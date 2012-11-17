#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <omp.h>
#include <glib.h>
#include "common.c"

#ifndef BLOCK_SIZE_ADJ
#define BLOCK_SIZE_ADJ 1
#endif

const int NUM_ALL = 10*1024*1024;
const int BLOCK_SIZE = 1024*1024*BLOCK_SIZE_ADJ;
const int PREPARE_SORT_SIZE = 1024*1024;
const char PATH_ALLENTRIES[] = "all-entries";
const char PATH_ALLENTRIES_SORTED[] = "all-entries-sorted";
const char PATH_SORT_TMP_1[] = "tmp1";
const char PATH_SORT_TMP_2[] = "tmp2";
const char PATH_DIR_DATABASE[] = "database";

void make_allentries() {
	int pos = 0;
	FILE *fp = fopen(PATH_ALLENTRIES, "wb");
	initialize_daily_seed();
#ifdef _OPENMP
	printf("OpenMP enabled\n");
#endif
	ENTRY *result = malloc(BLOCK_SIZE * sizeof(ENTRY));
	while (pos < NUM_ALL) {
		printf("%.2f\r", (double)pos / NUM_ALL);
		fflush(stdout);
		int n = MIN(BLOCK_SIZE, NUM_ALL - pos);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
		for (int i = 0; i < n; i ++) {
			u32 seed = to_seed(pos + i);
			ENTRY entry;
			seed_to_entry(seed, &entry);
			result[i] = entry;
		}
		fwrite(result, sizeof(ENTRY), n, fp);
		pos += n;
	}
	fclose(fp);
	free(result);
}

int compare_u32(u32 a, u32 b) {
	return a < b ? -1 : (a == b ? 0 : 1);
}

int compare_entry(const ENTRY *a, const ENTRY *b) {
	if (public_id(a) != public_id(b)) {
		return compare_u32(public_id(a), public_id(b));
	} else {
		return compare_u32(a->daily_seed_index, b->daily_seed_index);
	}
}

int qsort_callback_entry(const void *a, const void *b) {
	return compare_entry((const ENTRY *)a, (const ENTRY*)b);
}

static void prepare_sort(void) {
	FILE *rf = fopen(PATH_ALLENTRIES, "rb");
	FILE *wf = fopen(PATH_ALLENTRIES_SORTED, "wb");
	ENTRY *buf = malloc(PREPARE_SORT_SIZE * sizeof(ENTRY));
	while (TRUE) {
		size_t n = fread(buf, sizeof(ENTRY), PREPARE_SORT_SIZE, rf);
		qsort(buf, n, sizeof(ENTRY), qsort_callback_entry);
		fwrite(buf, sizeof(ENTRY), n, wf);
		if (n < (size_t)PREPARE_SORT_SIZE) break;
	}
	fclose(rf);
	fclose(wf);
	free(buf);
}

typedef struct {
	FILE *fp;
	ENTRY last, current;
	int available;
} READER;

static void read_next(READER *reader) {
	size_t n;
	reader->last = reader->current;
	n = fread(&reader->current, sizeof(ENTRY), 1, reader->fp);
	reader->available = (n == 1);
}

static void init_reader(READER *reader, FILE *fp) {
	reader->fp = fp;
	memset(&reader->last, 0xff, sizeof(ENTRY));
	memset(&reader->current, 0xff, sizeof(ENTRY));
	reader->available = FALSE;
	read_next(reader);
}

static int end_of_run(READER *reader) {
	return !reader->available || compare_entry(&reader->current, &reader->last) < 0;
}

static void copy_to(READER *reader, FILE *fp) {
	fwrite(&reader->current, sizeof(ENTRY), 1, fp);
	read_next(reader);
}

static void copy_run_to(READER *reader, FILE *fp) {
	do {
		copy_to(reader, fp);
	} while (!end_of_run(reader));
}

static void divide(FILE *wf1, FILE *wf2, FILE *rf) {
	READER reader;
	init_reader(&reader, rf);
	for (;;) {
		if (!reader.available) break;
		copy_run_to(&reader, wf1);
		if (!reader.available) break;
		copy_run_to(&reader, wf2);
	}
}

static int merge(FILE *rf1, FILE *rf2, FILE *wf) {
	int run_count = 0;
	READER reader1, reader2;
	init_reader(&reader1, rf1);
	init_reader(&reader2, rf2);
	while (reader1.available && reader2.available) {
		for (;;) {
			READER *a, *b;
			if (compare_entry(&reader1.current, &reader2.current) < 0) {
				a = &reader1, b = &reader2;
			} else {
				a = &reader2, b = &reader1;
			}
			copy_to(a, wf);
			if (end_of_run(a)) {
				copy_run_to(b, wf);
				break;
			}
		}
		run_count ++;
	}
	while (reader1.available) {
		copy_run_to(&reader1, wf);
		run_count ++;
	}
	while (reader2.available) {
		copy_run_to(&reader2, wf);
		run_count ++;
	}
	printf("run_count = %d\n", run_count);
	return run_count;
}

static void merge_sort(void) {
	const int size = BLOCK_SIZE * sizeof(ENTRY);
	char *buf = malloc(size * 3);
	int run_count;
	// divideとmergeを繰り返す。ただし、高速化のためIOに大きなバッファを設定する
	do {
		for (int i = 0; i < 2; i ++) {
			FILE *tmp1, *tmp2, *sorted;
			tmp1 = fopen(PATH_SORT_TMP_1, i ? "rb" : "wb");
			tmp2 = fopen(PATH_SORT_TMP_2, i ? "rb" : "wb");
			sorted = fopen(PATH_ALLENTRIES_SORTED, i ? "wb" : "rb");
			setvbuf(tmp1, buf, _IOFBF, size);
			setvbuf(tmp2, buf + size, _IOFBF, size);
			setvbuf(sorted, buf + size * 2, _IOFBF, size);
			if (i == 0) {
				divide(tmp1, tmp2, sorted);
			} else {
				run_count = merge(tmp1, tmp2, sorted);
			}
			fclose(tmp1); fclose(tmp2); fclose(sorted);
		}
	} while (run_count != 1);
	free(buf);
	remove(PATH_SORT_TMP_1);
	remove(PATH_SORT_TMP_2);
}

static void separate_by_public_id(void) {
	mkdir(PATH_DIR_DATABASE, 0777);
	const int size = BLOCK_SIZE * sizeof(ENTRY);
	char *buf = malloc(size * 2);
	FILE *rf = fopen(PATH_ALLENTRIES_SORTED, "rb");
	FILE *wf = NULL;
	setvbuf(rf, buf, _IOFBF, size);
	int current_public_id = -1;
	while (TRUE) {
		ENTRY entry;
		size_t n = fread(&entry, sizeof(ENTRY), 1, rf);
		if (n == 0) break;
		if (public_id(&entry) != current_public_id) {
			if (wf) fclose(wf);
			char path[256];
			sprintf(path, "%s/%.4x", PATH_DIR_DATABASE, public_id(&entry));
			wf = fopen(path, "wb");
			setvbuf(wf, buf + size, _IOFBF, size);
			current_public_id = public_id(&entry);
			if ((current_public_id % 1000) == 0) {
				printf("%d\r", current_public_id);
				fflush(stdout);
			}
		}
		fwrite(&entry, sizeof(ENTRY), 1, wf);
	}
	if (wf) fclose(wf);
	fclose(rf);
}

void remove_database_dir() {
	char str[256];
	sprintf(str, "rm -r %s", PATH_DIR_DATABASE);
	printf("removing database dir\n");
	system(str);
}

#define LOG(expr) do { \
	printf("start %s\n", #expr); \
	gint64 start = g_get_real_time(); \
	expr; \
	printf("finish %s (%.2f sec)\n", #expr, (g_get_real_time() - start) / 1e6); \
} while (0)

int main(void) {
	setvbuf(stdout, NULL, _IOLBF, 0); // 出力がteeされたときでもすぐにフラッシュされるように
	printf("BLOCK_SIZE = %d\n", BLOCK_SIZE);
	remove_database_dir();
	gint64 start = g_get_real_time();
	LOG(make_allentries());
	LOG(prepare_sort());
	LOG(merge_sort());
	LOG(separate_by_public_id());
	printf("total: %.2f sec\n", (g_get_real_time() - start) / 1e6);
}
