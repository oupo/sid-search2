// all-entries-sortedまではdaily_seed_indexをもたずに作る。separate_by_public_idの時点で付け加える。
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

typedef struct {
	u32 seed;
	u32 trainer_id;
} BRIEF_ENTRY;

typedef struct {
	u32 seed;
	u32 trainer_id;
	u32 daily_seed_index;
} ENTRY;

int brief_entry_public_id(const BRIEF_ENTRY *entry) {
	return entry->trainer_id & 0xffff;
}

int public_id(const ENTRY *entry) {
	return entry->trainer_id & 0xffff;
}

void seed_to_brief_entry(u32 seed, BRIEF_ENTRY *entry) {
	entry->seed = seed;
	entry->trainer_id = get_second_mt_result(seed);
}

void brief_entry_to_entry(const BRIEF_ENTRY *bentry, ENTRY *entry) {
	entry->seed = bentry->seed;
	entry->trainer_id = bentry->trainer_id;
	entry->daily_seed_index = daily_seed_to_index(get_first_mt_result(entry->seed));
}

void make_allentries() {
	int pos = 0;
	FILE *fp = fopen(PATH_ALLENTRIES, "wb");
#ifdef _OPENMP
	printf("OpenMP enabled\n");
#endif
	BRIEF_ENTRY *result = malloc(BLOCK_SIZE * sizeof(BRIEF_ENTRY));
	while (pos < NUM_ALL) {
		printf("%.2f\r", (double)pos / NUM_ALL);
		fflush(stdout);
		int n = MIN(BLOCK_SIZE, NUM_ALL - pos);
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
		for (int i = 0; i < n; i ++) {
			u32 seed = to_seed(pos + i);
			BRIEF_ENTRY entry;
			seed_to_brief_entry(seed, &entry);
			result[i] = entry;
		}
		fwrite(result, sizeof(BRIEF_ENTRY), n, fp);
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

int compare_brief_entry(const BRIEF_ENTRY *a, const BRIEF_ENTRY *b) {
	return compare_u32(brief_entry_public_id(a), brief_entry_public_id(b));
}

int qsort_callback_brief_entry(const void *a, const void *b) {
	return compare_brief_entry((const BRIEF_ENTRY *)a, (const BRIEF_ENTRY*)b);
}

static void prepare_sort(void) {
	FILE *rf = fopen(PATH_ALLENTRIES, "rb");
	FILE *wf = fopen(PATH_ALLENTRIES_SORTED, "wb");
	BRIEF_ENTRY *buf = malloc(PREPARE_SORT_SIZE * sizeof(BRIEF_ENTRY));
	while (TRUE) {
		size_t n = fread(buf, sizeof(BRIEF_ENTRY), PREPARE_SORT_SIZE, rf);
		qsort(buf, n, sizeof(BRIEF_ENTRY), qsort_callback_brief_entry);
		fwrite(buf, sizeof(BRIEF_ENTRY), n, wf);
		if (n < (size_t)PREPARE_SORT_SIZE) break;
	}
	fclose(rf);
	fclose(wf);
	free(buf);
}

typedef struct {
	FILE *fp;
	BRIEF_ENTRY last, current;
	int available;
} READER;

static void read_next(READER *reader) {
	size_t n;
	reader->last = reader->current;
	n = fread(&reader->current, sizeof(BRIEF_ENTRY), 1, reader->fp);
	reader->available = (n == 1);
}

static void init_reader(READER *reader, FILE *fp) {
	reader->fp = fp;
	memset(&reader->last, 0xff, sizeof(BRIEF_ENTRY));
	memset(&reader->current, 0xff, sizeof(BRIEF_ENTRY));
	reader->available = FALSE;
	read_next(reader);
}

static int end_of_run(READER *reader) {
	return !reader->available || compare_brief_entry(&reader->current, &reader->last) < 0;
}

static void copy_to(READER *reader, FILE *fp) {
	fwrite(&reader->current, sizeof(BRIEF_ENTRY), 1, fp);
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
			if (compare_brief_entry(&reader1.current, &reader2.current) < 0) {
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
	const int size = BLOCK_SIZE * sizeof(BRIEF_ENTRY);
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
	initialize_daily_seed();
	mkdir(PATH_DIR_DATABASE, 0777);
	GArray *array = g_array_sized_new(FALSE, FALSE, sizeof(ENTRY), 256*32);
	const int size = BLOCK_SIZE * sizeof(ENTRY);
	char *buf = malloc(size);
	FILE *rf = fopen(PATH_ALLENTRIES_SORTED, "rb");
	setvbuf(rf, buf, _IOFBF, size);
	int current_public_id = -1;
	while (TRUE) {
		BRIEF_ENTRY bentry;
		size_t n = fread(&bentry, sizeof(BRIEF_ENTRY), 1, rf);

		if (current_public_id == -1) {
			current_public_id = brief_entry_public_id(&bentry);
		} else if (n == 0 || brief_entry_public_id(&bentry) != current_public_id) {
			printf("array: ------------------\n");
			for (int i = 0; i < array->len; i ++) {
				ENTRY entry = ((ENTRY*)array->data)[i];
				printf("%3d: %.8x %.8x %.8x\n", i, entry.seed, entry.trainer_id, entry.daily_seed_index);
			}
			//g_array_sort(array, qsort_callback_entry);
			qsort(array->data, array->len, sizeof(ENTRY), qsort_callback_entry);
			char path[256];
			sprintf(path, "%s/%.4x", PATH_DIR_DATABASE, current_public_id);
			FILE *wf = fopen(path, "wb");
			printf("sorted: ------------------\n");
			for (int i = 0; i < array->len; i ++) {
				ENTRY entry = ((ENTRY*)array->data)[i];
				printf("%3d: %.8x %.8x %.8x\n", i, entry.seed, entry.trainer_id, entry.daily_seed_index);
			}
			fwrite(array->data, sizeof(ENTRY), g_array_get_element_size(array), wf);
			fclose(wf);
			g_array_set_size(array, 0);
			current_public_id = brief_entry_public_id(&bentry);
			if ((current_public_id % 1000) == 0) {
				printf("%d\r", current_public_id);
				fflush(stdout);
			}
			break;
		}
		if (n == 0) break;
		ENTRY entry;
		brief_entry_to_entry(&bentry, &entry);
		printf("%3d: %.8x %.8x %.8x\n", array->len, entry.seed, entry.trainer_id, entry.daily_seed_index);
		g_array_append_val(array, entry);
	}
	fclose(rf);
	g_array_unref(array);
}

void remove_database_dir() {
	char str[256];
	sprintf(str, "rm -r %s", PATH_DIR_DATABASE);
	printf("removing database dir\n");
	int r = system(str);
	g_assert(r >= 0); // warn_unused_result対策
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
	//LOG(make_allentries());
	//LOG(prepare_sort());
	//LOG(merge_sort());
	LOG(separate_by_public_id());
	printf("total: %.2f sec\n", (g_get_real_time() - start) / 1e6);
}
