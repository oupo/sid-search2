#include <stdint.h>
typedef uint32_t u32;

u32 next_mt_elem(u32 a, u32 i) {
	return 1812433253 * (a ^ (a >> 30)) + i;
}

u32 genrand(u32 mt0, u32 mt1, u32 mt397) {
	u32 v;
	v = (mt0 & 0x80000000) | (mt1 & 0x7fffffff);
	v = mt397 ^ (v >> 1) ^ ((v & 1) ? 0x9908b0df : 0);
	v ^=  v >> 11;
	v ^= (v <<  7) & 0x9d2c5680;
	v ^= (v << 15) & 0xefc60000;
	v ^=  v >> 18;
	return v;
}

void get_mt_result(u32 seed, u32 *ret1, u32 *ret2) {
	u32 mt, mt0, mt1, mt2, mt397, mt398;
	int i;
	mt0 = seed;
	mt1 = next_mt_elem(mt0, 1);
	mt2 = next_mt_elem(mt1, 2);
	mt = mt2;
	for (i = 3; i <= 397; i++) {
		mt = next_mt_elem(mt, i);
	}
	mt397 = mt;
	mt398 = next_mt_elem(mt, 398);
	
	*ret1 = genrand(mt0, mt1, mt397);
	*ret2 = genrand(mt1, mt2, mt398);
	return;
}

u32 get_first_mt_result(u32 seed) {
	u32 ret1, ret2;
	get_mt_result(seed, &ret1, &ret2);
	return ret1;
}

u32 get_second_mt_result(u32 seed) {
	u32 ret1, ret2;
	get_mt_result(seed, &ret1, &ret2);
	return ret2;
}

void daily_seed_make_const(u32 n, u32 *ret_a, u32 *ret_b) {
	const u32 A = 0x6c078965, B = 1;
	u32 a = A, b = B;
	u32 c = 1, d = 0;
	while (n) {
		if (n & 1) {
			d = d * a + b;
			c = c * a;
		}
		b = b * a + b;
		a = a * a;
		n >>= 1;
	}
	*ret_a = c, *ret_b = d;
}

u32 daily_seed_step_minus_2_pow_n(u32 seed, u32 n) {
	static u32 consts[32][2];
	static int initialized = 0;
	if (!initialized) {
		initialized = 1;
		for (int i = 0; i < 32; i ++) {
			daily_seed_make_const(-(1u<<i), &consts[i][0], &consts[i][1]);
		}
	}
	return seed * consts[n][0] + consts[n][1];
}

u32 daily_seed_to_index0(u32 seed, int i) {
	if (i == 32) {
		return 0;
	} else if ((seed >> i) & 1) {
		return daily_seed_to_index0(daily_seed_step_minus_2_pow_n(seed, i), i + 1) * 2 + 1;
	} else {
		return daily_seed_to_index0(seed, i + 1) * 2;
	}
}

u32 daily_seed_to_index(u32 seed) {
	return daily_seed_to_index0(seed, 0);
}

typedef struct {
	u32 seed;
	u32 trainer_id;
	u32 daily_seed_index;
} ENTRY;

#include <stdio.h>

int main(void) {
	int result = 0;
	for (u32 i = 0; i < 0x10000; i ++) {
		u32 trainer_id, daily_seed, daily_seed_index;
		get_mt_result(i, &trainer_id, &daily_seed);
		daily_seed_index = daily_seed_to_index(daily_seed);
		result += daily_seed_index; // 最適化防止
	}
	printf("%.x\n", result);
	printf("ok\n");
	return 0;
}
