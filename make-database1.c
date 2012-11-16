#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <omp.h>
#include "common.c"
#define min(a,b) ((a) < (b) ? (a) : (b))

const int NUM_ALL = 1024*1024;
const int BLOCK_SIZE = 1024*1024/4;
const char PATH_ALLENTRIES[] = "all-entries";
const char PATH_ALLENTRIES_SORTED[] = "all-entries-sorted";
const char PATH_SORT_TMP_1[] = "tmp1";
const char PATH_SORT_TMP_2[] = "tmp2";
const char PATH_DIR_DATABASE[] = "database";

char *now_time_str() {
	time_t t;
	struct tm *tm;
	static char str[256];
	t = time(NULL);
	tm = localtime(&t);
	strftime(str, sizeof(str), "%F %T", tm);
	return str;
}

void make_allentries() {
	int pos = 0;
	FILE *fp = fopen(PATH_ALLENTRIES, "wb");
	initialize_daily_seed();
#ifdef _OPENMP
	printf("OpenMP enabled\n");
#endif
	while (pos < NUM_ALL) {
		printf("%d\n", pos);
		ENTRY result[BLOCK_SIZE];
		int n = min(BLOCK_SIZE, NUM_ALL - pos);
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
	while (1) {
		ENTRY buf[BLOCK_SIZE];
		size_t n = fread(buf, sizeof(ENTRY), BLOCK_SIZE, rf);
		qsort(buf, n, sizeof(ENTRY), qsort_callback_entry);
		fwrite(buf, sizeof(ENTRY), n, wf);
		if (n < BLOCK_SIZE) break;
	}
	fclose(rf);
	fclose(wf);
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

static void divide(void) {
	FILE *rf = fopen(PATH_ALLENTRIES_SORTED, "rb");
	FILE *wf1 = fopen(PATH_SORT_TMP_1, "wb");
	FILE *wf2 = fopen(PATH_SORT_TMP_2, "wb");
	READER reader;
	init_reader(&reader, rf);
	for (;;) {
		if (!reader.available) break;
		copy_run_to(&reader, wf1);
		if (!reader.available) break;
		copy_run_to(&reader, wf2);
	}
	fclose(rf);
	fclose(wf1);
	fclose(wf2);
}

static int merge(void) {
	int run_count = 0;
	FILE *rf1 = fopen(PATH_SORT_TMP_1, "rb");
	FILE *rf2 = fopen(PATH_SORT_TMP_2, "rb");
	FILE *wf = fopen(PATH_ALLENTRIES_SORTED, "wb");
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
	fclose(rf1);
	fclose(rf2);
	fclose(wf);
	printf("run_count = %d\n", run_count);
	return run_count;
}

static void merge_sort(void) {
	int run_count;
	do {
		divide();
		run_count = merge();
	} while (run_count != 1);
	remove(PATH_SORT_TMP_1);
	remove(PATH_SORT_TMP_2);
}

static void separate_by_public_id(void) {
	mkdir(PATH_DIR_DATABASE, 0777);
	FILE *rf = fopen(PATH_ALLENTRIES_SORTED, "rb");
	int current_public_id = -1;
	FILE *wf = NULL;
	while (TRUE) {
		ENTRY entry;
		size_t n = fread(&entry, sizeof(ENTRY), 1, rf);
		if (n == 0) break;
		if (public_id(&entry) != current_public_id) {
			if (wf) fclose(wf);
			char path[256];
			sprintf(path, "%s/%.4x", PATH_DIR_DATABASE, public_id(&entry));
			wf = fopen(path, "wb");
			current_public_id = public_id(&entry);
		}
		fwrite(&entry, sizeof(ENTRY), 1, wf);
	}
	if (wf) fclose(wf);
	fclose(rf);
}

#define LOG(expr) do { \
	printf("(%s): start %s\n", now_time_str(), #expr); \
	expr; \
	printf("(%s): finish %s\n", now_time_str(), #expr); \
} while (0)

int main(void) {
	//LOG(make_allentries());
	LOG(prepare_sort());
	LOG(merge_sort());
	//LOG(separate_by_public_id());
}
