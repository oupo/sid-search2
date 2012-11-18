function u32(x) { return x >>> 0; }

function mul(a, b) {
	var a1 = a >>> 16, a2 = a & 0xffff;
	var b1 = b >>> 16, b2 = b & 0xffff;
	return u32(((a1 * b2 + a2 * b1) << 16) + a2 * b2);
}

function next_mt_elem(a, i) {
	return u32(mul(1812433253, (a ^ (a >>> 30))) + i);
}

function genrand(mt0, mt1, mt397) {
	var v;
	v = (mt0 & 0x80000000) | (mt1 & 0x7fffffff);
	v = mt397 ^ (v >>> 1) ^ ((v & 1) ? 0x9908b0df : 0);
	v ^=  v >>> 11;
	v ^= (v <<  7) & 0x9d2c5680;
	v ^= (v << 15) & 0xefc60000;
	v ^=  v >>> 18;
	return u32(v);
}

function get_mt_result(seed) {
	var mt, mt0, mt1, mt2, mt397, mt398;
	mt0 = seed;
	mt1 = next_mt_elem(mt0, 1);
	mt2 = next_mt_elem(mt1, 2);
	mt = mt2;
	for (var i = 3; i <= 397; i++) {
		mt = next_mt_elem(mt, i);
	}
	mt397 = mt;
	mt398 = next_mt_elem(mt, 398);
	
	var ret1 = genrand(mt0, mt1, mt397);
	var ret2 = genrand(mt1, mt2, mt398);
	return [ret1, ret2];
}

function get_first_mt_result(seed) {
	return get_mt_result(seed)[0];
}

function get_second_mt_result(seed) {
	return get_mt_result(seed)[1];
}

function daily_seed_make_const(n) {
	var A = 0x6c078965, B = 1;
	var a = A, b = B;
	var c = 1, d = 0;
	while (n) {
		if (n & 1) {
			d = u32(mul(d, a) + b);
			c = mul(c, a);
		}
		b = u32(mul(b, a) + b);
		a = mul(a, a);
		n >>>= 1;
	}
	return [c, d];
}

var daily_seed_step_minus_2_pow_n = (function() {
	var consts;

	function initialize() {
		if (consts) return;
		consts = [];
		for (var i = 0; i < 32; i ++) {
			consts[i] = daily_seed_make_const(-(1<<i));
		}
	}

	return function daily_seed_step_minus_2_pow_n(seed, n) {
		initialize();
		return u32(mul(seed, consts[n][0]) + consts[n][1]);
	}
})();

function daily_seed_stepper(n) {
	var c = daily_seed_make_const(n);
	return function (seed) {
		return u32(mul(seed, c[0]) + c[1]);
	};
}

function daily_seed_step(seed, n) {
	return daily_seed_stepper(n)(seed);
}

var daily_seed_next = daily_seed_stepper(1);
var daily_seed_prev = daily_seed_stepper(-1);

function daily_seed_to_index0(seed, i) {
	if (i === 32) {
		return 0;
	} else if ((seed >>> i) & 1) {
		return daily_seed_to_index0(daily_seed_step_minus_2_pow_n(seed, i), i + 1) * 2 + 1;
	} else {
		return daily_seed_to_index0(seed, i + 1) * 2;
	}
}

function daily_seed_to_index(seed) {
	return daily_seed_to_index0(seed, 0);
}

function daily_seed_to_lottery_seed(seed) {
	return u32(mul(seed, 0x41c64e6d) + 0x3039);
}

function lottery_seed_to_daily_seed(seed) {
	return u32(mul(seed, 0xeeb9eb65) + 0xfc77a683);
}

function lottery_numbers_to_daily_seeds(numbers) {
	var ret = [];
	for (var i = 0; i < 65536; i ++) {
		var lottery_seed = u32(numbers[0] << 16 | i);
		var fseed = lottery_seed_to_daily_seed(lottery_seed);
		var seed = fseed;
		for (var j = 1; j < numbers.length; j ++) {
			seed = daily_seed_next(seed);
			var number = daily_seed_to_lottery_seed(seed) >>> 16;
			if (numbers[j] !== number) break;
		}
		if (j === numbers.length) {
			ret.push(fseed);
		}
	}
	return ret;
}
