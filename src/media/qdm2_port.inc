// ---- ScummVM math/cosinetables.h



namespace Math {

/**
 * @defgroup math_cosinetables Cosine tables
 * @ingroup math
 *
 * @brief  Functions for working with cosine tables.
 *
 * @{
 */

class CosineTable {
public:
	/**
	 * Construct a cosine table given the number of points.
	 *
	 * @param nPoints Number of distinct radian points that must be in range [16,65536] and be divisible by 4.
	 */
	CosineTable(int nPoints);
	~CosineTable();

	/**
	 * Get a pointer to a table.
	 *
	 * This table contains nPoints/2 entries.
	 * Prefer to use at()
	 * The layout of this table is as follows:
	 * - Entries 0 up to (including) nPoints/4:
	 *           cos(0) till (including) cos(1/2*pi)
	 * - Entries (excluding) nPoints/4 up to nPoints/2:
	 *           (excluding) cos(3/2*pi) till (excluding) cos(2*pi)
	 */
	const float *getTable() { return _tableEOS; }

	/**
	 * Return cos(2*pi * index / nPoints )
	 * Index must be in range [0, nPoints - 1]
	 * Faster than atLegacy.
	 */
	float at(int index) const;

	/**
	 * Returns cos(2*pi * index / nPoints )
	 * Index must be in range [0, nPoints - 1]
	 */
	float atLegacy(int index) const;

private:
	float *_tableEOS;
	float *_table;
	double _radResolution; // Smallest radian increment
	int _refSize; // _nPoints / 4
	int _nPoints; // range of operator[]
};

/** @} */

} // End of namespace Math


// ---- ScummVM math/sinetables.h



namespace Math {

/**
 * @defgroup math_sinetables Sine tables
 * @ingroup math
 *
 * @brief API for managing sine tables.
 *
 * @{
 */

class SineTable {
public:
	/**
	 * Construct a sine table given the number of points
	 *
	 * @param nPoints Number of distinct radian points, which must be in range [16,65536] and be divisible by 4
	 */
	SineTable(int nPoints);
	~SineTable();

	/**
	 * Get pointer to table
	 *
	 * This table contains nPoints/2 entries.
	 * Prefer to use at()
	 * The layout of this table is as follows:
	 * - Entries 0 up to (excluding) nPoints/4:
	 *           sin(0) till (excluding) sin(1/2*pi)
	 * - Entries 2_nPoints/4 up to nPoints/2:
	 *           sin(pi) till (excluding) sin(3/2*pi)
	 */
	const float *getTable() { return _tableEOS; }

	/**
	 * Returns sin(2*pi * index / nPoints )
	 * Index must be in range [0, nPoints - 1]
	 * Faster than atLegacy
	 */
	float at(int index) const;

	/**
	 * Returns sin(2*pi * index / nPoints )
	 * Index must be in range [0, nPoints - 1]
	 */
	float atLegacy(int index) const;

private:
	float *_tableEOS;
	float *_table;
	double _radResolution; // Smallest radian increment
	int _refSize; // _nPoints / 4
	int _nPoints; // range of operator[]
};

/** @} */

} // End of namespace Math


// ---- ScummVM math/cosinetables.cpp

// Based on eos' cosine tables


namespace Math {

CosineTable::CosineTable(int nPoints) {
	assert((nPoints >= 16) && (nPoints <= 65536)); // log2 space is in [4,16]
	assert(nPoints % 4 == 0);

	_nPoints = nPoints;
	_radResolution = 2.0 * M_PI / _nPoints;
	_refSize = _nPoints / 4;
	_tableEOS = new float[_nPoints / 2];
	_table = new float[_nPoints];

	for (int i = 0; i < _nPoints; i++)
		_table[i] = cos(i * _radResolution);

	// Table contains cos(2*pi*i/_nPoints) for 0<=i<=_nPoints/4,
	// followed by 3_nPoints/4<=i<_nPoints
	for (int i = 0; i <= _nPoints / 4; i++)
		_tableEOS[i] = cos(i * _radResolution);

	for (int i = 1; i < _nPoints / 4; i++)
		_tableEOS[_nPoints / 2 - i] = _tableEOS[i];
}

float CosineTable::at(int index) const {
	assert((index >= 0) && (index < _nPoints));
	return _table[index];
}

float CosineTable::atLegacy(int index) const {
	assert((index >= 0) && (index < _nPoints));
	if (index < _refSize)
		// [0,pi/2)
		return _tableEOS[index];
	if ((index > _refSize) && (index < 2 * _refSize))
		// (pi/2,pi)
		return -_tableEOS[2 * _refSize - index];
	if ((index >= 2 * _refSize) && (index < 3 * _refSize))
		// [pi,3/2pi)
		return -_tableEOS[index - 2 * _refSize];
	if ((index > 3 * _refSize) && (index < _nPoints))
		// (3/2pi,2pi)
		return _tableEOS[_nPoints - index];
	return 0.0f; // cos(pi/2) and cos(3pi/2) = 0
}

CosineTable::~CosineTable() {
	delete[] _tableEOS;
	delete[] _table;
}

} // End of namespace Math

// ---- ScummVM math/sinetables.cpp

// Based on eos' sine tables


namespace Math {

SineTable::SineTable(int nPoints) {
	assert((nPoints >= 16) && (nPoints <= 65536)); // log2 space is in [4,16]
	assert(nPoints % 4 == 0);

	_nPoints = nPoints;
	_radResolution = 2.0 * M_PI / _nPoints;
	_refSize = _nPoints / 4;
	_tableEOS = new float[_nPoints / 2];
	_table = new float[_nPoints];

	for (int i = 0; i < _nPoints; i++)
		_table[i] = sin(i * _radResolution);

	// Table contains sin(2*pi*i/_nPoints) for 0<=i<_nPoints/4,
	// followed by _nPoints/2<=i<3_nPoints/4
	for (int i = 0; i < _nPoints / 4; i++)
		_tableEOS[i] = sin(i * _radResolution);

	for (int i = 0; i < _nPoints / 4; i++)
		_tableEOS[_nPoints / 4 + i] = -_tableEOS[i];
}

float SineTable::at(int index) const {
	assert((index >= 0) && (index < _nPoints));
	return _table[index];
}

float SineTable::atLegacy(int index) const {
	assert((index >= 0) && (index < _nPoints));
	if (index < _refSize)
		// [0,pi/2)
		return _tableEOS[index];
	if (index == _refSize)
		// pi/2
		return 1.0f; // sin(pi/2) = 1.0
	if ((index > _refSize) && (index < 2 * _refSize))
		// (pi/2,pi)
		return _tableEOS[2 * _refSize - index];
	if ((index >= 2 * _refSize) && (index < 3 * _refSize))
		// [pi,3/2pi)
		return -_tableEOS[index - 2 * _refSize];
	if ((index > 3 * _refSize) && (index < _nPoints))
		// (3/2pi,2pi)
		return -_tableEOS[_nPoints - index];
	return -1.0f; // sin(3pi/2) = -1.0
}

SineTable::~SineTable() {
	delete[] _tableEOS;
	delete[] _table;
}

} // End of namespace Math

// ---- ScummVM math/fft.h

// Based on eos' (I)FFT code which is in turn
// based upon the (I)FFT code in FFmpeg
// Copyright (c) 2008 Loren Merritt
// Copyright (c) 2002 Fabrice Bellard
// Partly based on libdjbfft by D. J. Bernstein



namespace Math {

/**
 * @defgroup math_fft Fast Fourier Transform (FFT)
 * @ingroup math
 *
 * @brief  API for the FFT algorithm.
 *
 * @{
 */

class CosineTable;
struct Complex;

/**
 * (Inverse) Fast Fourier Transform.
 *
 * Used in engines:
 *  - SCUMM
 */
class FFT {
public:
	FFT(int bits, int inverse);
	~FFT();

	const uint16 *getRevTab() const;

	/** Perform the permutation needed BEFORE calling calc(). */
	void permute(Complex *z);

	/** Perform a complex FFT.
	 *
	 *  The input data must be permuted before.
	 *  No 1.0/sqrt(n) normalization is done.
	 */
	void calc(Complex *z);

private:
	int _bits;
	int _inverse;

	uint16 *_revTab;

	Complex *_expTab;
	Complex *_tmpBuf;

	int _splitRadix;

	static int splitRadixPermutation(int i, int n, int inverse);

	CosineTable *_cosTables[13];

	void fft4(Complex *z);
	void fft8(Complex *z);
	void fft16(Complex *z);
	void fft(int n, int logn, Complex *z);
};

/** @} */

} // End of namespace Math


// ---- ScummVM math/fft.cpp

// Based on eos' (I)FFT code which is in turn
// Based upon the (I)FFT code in FFmpeg
// Copyright (c) 2008 Loren Merritt
// Copyright (c) 2002 Fabrice Bellard
// Partly based on libdjbfft by D. J. Bernstein


namespace Math {

FFT::FFT(int bits, int inverse) : _bits(bits), _inverse(inverse) {
	assert((_bits >= 2) && (_bits <= 16));

	int n = 1 << bits;
	int nPoints;

	_tmpBuf = new Complex[n];
	_expTab = new Complex[n / 2];
	_revTab = new uint16[n];

	_splitRadix = 1;

	for (int i = 0; i < n; i++)
		_revTab[-splitRadixPermutation(i, n, _inverse) & (n - 1)] = i;

	for (int i = 0; i < ARRAYSIZE(_cosTables); i++) {
		if (i + 4 <= _bits) {
			nPoints = 1 << (i + 4);
			_cosTables[i] = new CosineTable(nPoints);
		}
		else
			_cosTables[i] = nullptr;
	}
}

FFT::~FFT() {
	for (int i = 0; i < ARRAYSIZE(_cosTables); i++) {
		delete _cosTables[i];
	}

	delete[] _revTab;
	delete[] _expTab;
	delete[] _tmpBuf;
}

const uint16 *FFT::getRevTab() const {
	return _revTab;
}

void FFT::permute(Complex *z) {
	int np = 1 << _bits;

	if (_tmpBuf) {
		for (int j = 0; j < np; j++)
			_tmpBuf[_revTab[j]] = z[j];

		memcpy(z, _tmpBuf, np * sizeof(Complex));

		return;
	}

	// Reverse
	for (int j = 0; j < np; j++) {
		int k = _revTab[j];

		if (k < j)
			SWAP(z[k], z[j]);
	}
}

int FFT::splitRadixPermutation(int i, int n, int inverse) {
	if (n <= 2)
		return i & 1;

	int m = n >> 1;

	if (!(i & m))
		return splitRadixPermutation(i, m, inverse) * 2;

	m >>= 1;

	if (inverse == !(i & m))
		return splitRadixPermutation(i, m, inverse) * 4 + 1;

	return splitRadixPermutation(i, m, inverse) * 4 - 1;
}

#define sqrthalf (float)M_SQRT1_2

#define BF(x, y, a, b) { \
	x = a - b; \
	y = a + b; \
}

#define BUTTERFLIES(a0, a1, a2, a3) { \
	BF(t3, t5, t5, t1); \
	BF(a2.re, a0.re, a0.re, t5); \
	BF(a3.im, a1.im, a1.im, t3); \
	BF(t4, t6, t2, t6); \
	BF(a3.re, a1.re, a1.re, t4); \
	BF(a2.im, a0.im, a0.im, t6); \
}

// force loading all the inputs before storing any.
// this is slightly slower for small data, but avoids store->load aliasing
// for addresses separated by large powers of 2.
#define BUTTERFLIES_BIG(a0, a1, a2, a3) { \
	float r0 = a0.re, i0 = a0.im, r1 = a1.re, i1 = a1.im; \
	BF(t3, t5, t5, t1); \
	BF(a2.re, a0.re, r0, t5); \
	BF(a3.im, a1.im, i1, t3); \
	BF(t4, t6, t2, t6); \
	BF(a3.re, a1.re, r1, t4); \
	BF(a2.im, a0.im, i0, t6); \
}

#define TRANSFORM(a0, a1, a2, a3, wre, wim) { \
	t1 = a2.re * wre + a2.im * wim; \
	t2 = a2.im * wre - a2.re * wim; \
	t5 = a3.re * wre - a3.im * wim; \
	t6 = a3.im * wre + a3.re * wim; \
	BUTTERFLIES(a0, a1, a2, a3) \
}

#define TRANSFORM_ZERO(a0, a1, a2, a3) { \
	t1 = a2.re; \
	t2 = a2.im; \
	t5 = a3.re; \
	t6 = a3.im; \
	BUTTERFLIES(a0, a1, a2, a3) \
}

/* z[0...8n-1], w[1...2n-1] */
#define PASS(name) \
static void name(Complex *z, const float *wre, unsigned int n) { \
	float t1, t2, t3, t4, t5, t6; \
	int o1 = 2 * n; \
	int o2 = 4 * n; \
	int o3 = 6 * n; \
	const float *wim = wre + o1; \
	n--; \
	\
	TRANSFORM_ZERO(z[0], z[o1], z[o2], z[o3]); \
	TRANSFORM(z[1], z[o1 + 1], z[o2 + 1], z[o3 + 1], wre[1], wim[-1]); \
	do { \
		z += 2; \
		wre += 2; \
		wim -= 2; \
		TRANSFORM(z[0], z[o1], z[o2], z[o3], wre[0], wim[0]);\
		TRANSFORM(z[1], z[o1 + 1], z[o2 + 1], z[o3 + 1], wre[1], wim[-1]);\
	} while(--n);\
}

PASS(pass)
#undef BUTTERFLIES
#define BUTTERFLIES BUTTERFLIES_BIG
PASS(pass_big)

void FFT::fft4(Complex *z) {
	float t1, t2, t3, t4, t5, t6, t7, t8;

	BF(t3, t1, z[0].re, z[1].re);
	BF(t8, t6, z[3].re, z[2].re);
	BF(z[2].re, z[0].re, t1, t6);
	BF(t4, t2, z[0].im, z[1].im);
	BF(t7, t5, z[2].im, z[3].im);
	BF(z[3].im, z[1].im, t4, t8);
	BF(z[3].re, z[1].re, t3, t7);
	BF(z[2].im, z[0].im, t2, t5);
}

void FFT::fft8(Complex *z) {
	float t1, t2, t3, t4, t5, t6, t7, t8;

	fft4(z);

	BF(t1, z[5].re, z[4].re, -z[5].re);
	BF(t2, z[5].im, z[4].im, -z[5].im);
	BF(t3, z[7].re, z[6].re, -z[7].re);
	BF(t4, z[7].im, z[6].im, -z[7].im);
	BF(t8, t1, t3, t1);
	BF(t7, t2, t2, t4);
	BF(z[4].re, z[0].re, z[0].re, t1);
	BF(z[4].im, z[0].im, z[0].im, t2);
	BF(z[6].re, z[2].re, z[2].re, t7);
	BF(z[6].im, z[2].im, z[2].im, t8);

	TRANSFORM(z[1], z[3], z[5], z[7], sqrthalf, sqrthalf);
}

void FFT::fft16(Complex *z) {
	float t1, t2, t3, t4, t5, t6;

	fft8(z);
	fft4(z + 8);
	fft4(z + 12);

	assert(_cosTables[0]);
	const float * const cosTable = _cosTables[0]->getTable();

	TRANSFORM_ZERO(z[0], z[4], z[8], z[12]);
	TRANSFORM(z[2], z[6], z[10], z[14], sqrthalf, sqrthalf);
	TRANSFORM(z[1], z[5], z[9], z[13], cosTable[1],cosTable[3]);
	TRANSFORM(z[3], z[7], z[11], z[15], cosTable[3], cosTable[1]);
}

void FFT::fft(int n, int logn, Complex *z) {
	switch (logn) {
	case 2:
		fft4(z);
		break;
	case 3:
		fft8(z);
		break;
	case 4:
		fft16(z);
		break;
	default:
		fft((n / 2), logn - 1, z);
		fft((n / 4), logn - 2, z + (n / 4) * 2);
		fft((n / 4), logn - 2, z + (n / 4) * 3);
		assert(_cosTables[logn - 4]);
		if (n > 1024)
			pass_big(z, _cosTables[logn - 4]->getTable(), (n / 4) / 2);
		else
			pass(z, _cosTables[logn - 4]->getTable(), (n / 4) / 2);
	}
}

void FFT::calc(Complex *z) {
	fft(1 << _bits, _bits, z);
}

} // End of namespace Math

// ---- ScummVM math/rdft.h

// Based on xoreos' (I)RDFT code which is in turn
// Based upon the (I)RDFT code in FFmpeg
// Copyright (c) 2009 Alex Converse <alex dot converse at gmail dot com>



namespace Math {

class FFT;

/**
 * @defgroup math_rdft RDFT algorithm
 * @ingroup math
 *
 * @brief API for the Real Discrete Fourier Transform (RDFT) algorithm.
 *
 * @{
 */

/**
 * @brief (Inverse) Real Discrete Fourier Transform.
 *
 * @details Used in audio:
 *  - QDM2
 *
 * Used in engines:
 *  - scumm
 *
 *
 * It has four modes:
 *
 *   Below, n = 1 << bits
 *
 *   (I)DFT_R2C:
 *     input:
 *       n real floats
 *     output:
 *       n/2 complex floats (stored as real part followed by imag part).
 *
 *     The output represents the first half of the (I)DFT of the input.
 *     If F is the complex (I)DFT of the input, then
 *     output[0] = F[0] + i * F[n/2] and
 *     output[k] = F[k] for k = 1 .. n/2-1.
 *     Note that F[0] and F[k] are real since the input is real, and
 *     the remaining values of F can be reconstructed from symmetry if desired.
 *
 *   (I)DFT_C2R:
 *     input:
 *       n/2 complex floats
 *     output:
 *       n real floats
 *
 *     The input encodes a complex vector x of length n that has the
 *     required symmetry to have a real (I)DFT:
 *     x[0] = Re(input[0])
 *     x[k] = input[k] for k = 1 .. n/2-1
 *     x[n/2] = Im(input[0])
 *     x[k] = conj(input[n-k]) for k = n/2+1 .. n-1
 *     The output is then the real (I)DFT of x, divided by 2.
 *
 *     TODO: Is this division by 2 intentional?
 */

class RDFT {
public:
	enum TransformType {
		DFT_R2C,
		IDFT_C2R,
		IDFT_R2C,
		DFT_C2R
	};

	RDFT(int bits, TransformType trans);
	~RDFT();

	void calc(float *data);

private:
	int _bits;
	int _inverse;
	int _signConvention;

	SineTable _sin;
	CosineTable _cos;
	const float *_tSin;
	const float *_tCos;

	FFT *_fft;
};

/** @} */

} // End of namespace Math


// ---- ScummVM math/rdft.cpp

// Based on eos' (I)RDFT code which is in turn
// Based upon the (I)RDFT code in FFmpeg
// Copyright (c) 2009 Alex Converse <alex dot converse at gmail dot com>


namespace Math {

RDFT::RDFT(int bits, TransformType trans) : _bits(bits), _sin(1 << bits), _cos(1 << bits), _fft(nullptr) {
	assert((_bits >= 4) && (_bits <= 16));

	_inverse        = trans == IDFT_C2R || trans == DFT_C2R;
	_signConvention = trans == IDFT_R2C || trans == DFT_C2R ? 1 : -1;

	_fft = new FFT(bits - 1, trans == IDFT_C2R || trans == IDFT_R2C);

	int n = 1 << bits;

	_tSin = _sin.getTable() + (trans == DFT_R2C || trans == DFT_C2R) * (n >> 2);
	_tCos = _cos.getTable();
}

RDFT::~RDFT() {
	delete _fft;
}

void RDFT::calc(float *data) {
	const int n = 1 << _bits;

	const float k1 = 0.5f;
	const float k2 = 0.5f - _inverse;

	if (!_inverse) {
		_fft->permute((Complex *)data);
		_fft->calc   ((Complex *)data);
	}

	Complex ev, od;

	/* i=0 is a special case because of packing, the DC term is real, so we
	   are going to throw the N/2 term (also real) in with it. */

	ev.re = data[0];

	data[0] = ev.re + data[1];
	data[1] = ev.re - data[1];

	int i;
	for (i = 1; i < (n >> 2); i++) {
		int i1 = 2 * i;
		int i2 = n - i1;

		/* Separate even and odd FFTs */
		ev.re =  k1 * (data[i1    ] + data[i2   ]);
		od.im = -k2 * (data[i1    ] - data[i2   ]);
		ev.im =  k1 * (data[i1 + 1] - data[i2 + 1]);
		od.re =  k2 * (data[i1 + 1] + data[i2 + 1]);

		/* Apply twiddle factors to the odd FFT and add to the even FFT */
		data[i1    ] =  ev.re + od.re * _tCos[i] - od.im * _tSin[i];
		data[i1 + 1] =  ev.im + od.im * _tCos[i] + od.re * _tSin[i];
		data[i2    ] =  ev.re - od.re * _tCos[i] + od.im * _tSin[i];
		data[i2 + 1] = -ev.im + od.im * _tCos[i] + od.re * _tSin[i];
	}

	data[2 * i + 1] = _signConvention * data[2 * i + 1];

	if (_inverse) {
		data[0] *= k1;
		data[1] *= k1;

		_fft->permute((Complex *)data);
		_fft->calc   ((Complex *)data);
	}

}

} // End of namespace Math

// ---- ScummVM audio/decoders/qdm2data.h



namespace Audio {

/// VLC TABLES

// values in this table range from -1..23; adjust retrieved value by -1
static const uint16 vlc_tab_level_huffcodes[24] = {
	0x037c, 0x0004, 0x003c, 0x004c, 0x003a, 0x002c, 0x001c, 0x001a,
	0x0024, 0x0014, 0x0001, 0x0002, 0x0000, 0x0003, 0x0007, 0x0005,
	0x0006, 0x0008, 0x0009, 0x000a, 0x000c, 0x00fc, 0x007c, 0x017c
};

static const byte vlc_tab_level_huffbits[24] = {
	10, 6, 7, 7, 6, 6, 6, 6, 6, 5, 4, 4, 4, 3, 3, 3, 3, 4, 4, 5, 7, 8, 9, 10
};

// values in this table range from -1..36; adjust retrieved value by -1
static const uint16 vlc_tab_diff_huffcodes[37] = {
	0x1c57, 0x0004, 0x0000, 0x0001, 0x0003, 0x0002, 0x000f, 0x000e,
	0x0007, 0x0016, 0x0037, 0x0027, 0x0026, 0x0066, 0x0006, 0x0097,
	0x0046, 0x01c6, 0x0017, 0x0786, 0x0086, 0x0257, 0x00d7, 0x0357,
	0x00c6, 0x0386, 0x0186, 0x0000, 0x0157, 0x0c57, 0x0057, 0x0000,
	0x0b86, 0x0000, 0x1457, 0x0000, 0x0457
};

static const byte vlc_tab_diff_huffbits[37] = {
	13, 3, 3, 2, 3, 3, 4, 4, 6, 5, 6, 6, 7, 7, 8, 8,
	8, 9, 8, 11, 9, 10, 8, 10, 9, 12, 10, 0, 10, 13, 11, 0,
	12, 0, 13, 0, 13
};

// values in this table range from -1..5; adjust retrieved value by -1
static const byte vlc_tab_run_huffcodes[6] = {
	0x1f, 0x00, 0x01, 0x03, 0x07, 0x0f
};

static const byte vlc_tab_run_huffbits[6] = {
	5, 1, 2, 3, 4, 5
};

// values in this table range from -1..19; adjust retrieved value by -1
static const uint16 vlc_tab_tone_level_idx_hi1_huffcodes[20] = {
	0x5714, 0x000c, 0x0002, 0x0001, 0x0000, 0x0004, 0x0034, 0x0054,
	0x0094, 0x0014, 0x0114, 0x0214, 0x0314, 0x0614, 0x0e14, 0x0f14,
	0x2714, 0x0714, 0x1714, 0x3714
};

static const byte vlc_tab_tone_level_idx_hi1_huffbits[20] = {
	15, 4, 2, 1, 3, 5, 6, 7, 8, 10, 10, 11, 11, 12, 12, 12, 14, 14, 15, 14
};

// values in this table range from -1..23; adjust retrieved value by -1
static const uint16 vlc_tab_tone_level_idx_mid_huffcodes[24] = {
	0x0fea, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000, 0x03ea, 0x00ea, 0x002a, 0x001a,
	0x0006, 0x0001, 0x0000, 0x0002, 0x000a, 0x006a, 0x01ea, 0x07ea
};

static const byte vlc_tab_tone_level_idx_mid_huffbits[24] = {
	12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 11, 9, 7, 5, 3, 1, 2, 4, 6, 8, 10, 12
};

// values in this table range from -1..23; adjust retrieved value by -1
static const uint16 vlc_tab_tone_level_idx_hi2_huffcodes[24] = {
	0x0664, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0064, 0x00e4,
	0x00a4, 0x0068, 0x0004, 0x0008, 0x0014, 0x0018, 0x0000, 0x0001,
	0x0002, 0x0003, 0x000c, 0x0028, 0x0024, 0x0164, 0x0000, 0x0264
};

static const byte vlc_tab_tone_level_idx_hi2_huffbits[24] = {
	11, 0, 0, 0, 0, 0, 10, 8, 8, 7, 6, 6, 5, 5, 4, 2, 2, 2, 4, 7, 8, 9, 0, 11
};

// values in this table range from -1..8; adjust retrieved value by -1
static const byte vlc_tab_type30_huffcodes[9] = {
	0x3c, 0x06, 0x00, 0x01, 0x03, 0x02, 0x04, 0x0c, 0x1c
};

static const byte vlc_tab_type30_huffbits[9] = {
	6, 3, 3, 2, 2, 3, 4, 5, 6
};

// values in this table range from -1..9; adjust retrieved value by -1
static const byte vlc_tab_type34_huffcodes[10] = {
	0x18, 0x00, 0x01, 0x04, 0x05, 0x07, 0x03, 0x02, 0x06, 0x08
};

static const byte vlc_tab_type34_huffbits[10] = {
	5, 4, 3, 3, 3, 3, 3, 3, 3, 5
};

// values in this table range from -1..22; adjust retrieved value by -1
static const uint16 vlc_tab_fft_tone_offset_0_huffcodes[23] = {
	0x038e, 0x0001, 0x0000, 0x0022, 0x000a, 0x0006, 0x0012, 0x0002,
	0x001e, 0x003e, 0x0056, 0x0016, 0x000e, 0x0032, 0x0072, 0x0042,
	0x008e, 0x004e, 0x00f2, 0x002e, 0x0036, 0x00c2, 0x018e
};

static const byte vlc_tab_fft_tone_offset_0_huffbits[23] = {
	10, 1, 2, 6, 4, 5, 6, 7, 6, 6, 7, 7, 8, 7, 8, 8, 9, 7, 8, 6, 6, 8, 10
};

// values in this table range from -1..27; adjust retrieved value by -1
static const uint16 vlc_tab_fft_tone_offset_1_huffcodes[28] = {
	0x07a4, 0x0001, 0x0020, 0x0012, 0x001c, 0x0008, 0x0006, 0x0010,
	0x0000, 0x0014, 0x0004, 0x0032, 0x0070, 0x000c, 0x0002, 0x003a,
	0x001a, 0x002c, 0x002a, 0x0022, 0x0024, 0x000a, 0x0064, 0x0030,
	0x0062, 0x00a4, 0x01a4, 0x03a4
};

static const byte vlc_tab_fft_tone_offset_1_huffbits[28] = {
	11, 1, 6, 6, 5, 4, 3, 6, 6, 5, 6, 6, 7, 6, 6, 6,
	6, 6, 6, 7, 8, 6, 7, 7, 7, 9, 10, 11
};

// values in this table range from -1..31; adjust retrieved value by -1
static const uint16 vlc_tab_fft_tone_offset_2_huffcodes[32] = {
	0x1760, 0x0001, 0x0000, 0x0082, 0x000c, 0x0006, 0x0003, 0x0007,
	0x0008, 0x0004, 0x0010, 0x0012, 0x0022, 0x001a, 0x0000, 0x0020,
	0x000a, 0x0040, 0x004a, 0x006a, 0x002a, 0x0042, 0x0002, 0x0060,
	0x00aa, 0x00e0, 0x00c2, 0x01c2, 0x0160, 0x0360, 0x0760, 0x0f60
};

static const byte vlc_tab_fft_tone_offset_2_huffbits[32] = {
	13, 2, 0, 8, 4, 3, 3, 3, 4, 4, 5, 5, 6, 5, 7, 7,
	7, 7, 7, 7, 8, 8, 8, 9, 8, 8, 9, 9, 10, 11, 13, 12
};

// values in this table range from -1..34; adjust retrieved value by -1
static const uint16 vlc_tab_fft_tone_offset_3_huffcodes[35] = {
	0x33ea, 0x0005, 0x0000, 0x000c, 0x0000, 0x0006, 0x0003, 0x0008,
	0x0002, 0x0001, 0x0004, 0x0007, 0x001a, 0x000f, 0x001c, 0x002c,
	0x000a, 0x001d, 0x002d, 0x002a, 0x000d, 0x004c, 0x008c, 0x006a,
	0x00cd, 0x004d, 0x00ea, 0x020c, 0x030c, 0x010c, 0x01ea, 0x07ea,
	0x0bea, 0x03ea, 0x13ea
};

static const byte vlc_tab_fft_tone_offset_3_huffbits[35] = {
	14, 4, 0, 10, 4, 3, 3, 4, 4, 3, 4, 4, 5, 4, 5, 6,
	6, 5, 6, 7, 7, 7, 8, 8, 8, 8, 9, 10, 10, 10, 10, 11,
	12, 13, 14
};

// values in this table range from -1..37; adjust retrieved value by -1
static const uint16 vlc_tab_fft_tone_offset_4_huffcodes[38] = {
	0x5282, 0x0016, 0x0000, 0x0136, 0x0004, 0x0000, 0x0007, 0x000a,
	0x000e, 0x0003, 0x0001, 0x000d, 0x0006, 0x0009, 0x0012, 0x0005,
	0x0025, 0x0022, 0x0015, 0x0002, 0x0076, 0x0035, 0x0042, 0x00c2,
	0x0182, 0x00b6, 0x0036, 0x03c2, 0x0482, 0x01c2, 0x0682, 0x0882,
	0x0a82, 0x0082, 0x0282, 0x1282, 0x3282, 0x2282
};

static const byte vlc_tab_fft_tone_offset_4_huffbits[38] = {
	15, 6, 0, 9, 3, 3, 3, 4, 4, 3, 4, 4, 5, 4, 5, 6,
	6, 6, 6, 8, 7, 6, 8, 9, 9, 8, 9, 10, 11, 10, 11, 12,
	12, 12, 14, 15, 14, 14
};

/// FFT TABLES

// values in this table range from -1..27; adjust retrieved value by -1
static const uint16 fft_level_exp_alt_huffcodes[28] = {
	0x1ec6, 0x0006, 0x00c2, 0x0142, 0x0242, 0x0246, 0x00c6, 0x0046,
	0x0042, 0x0146, 0x00a2, 0x0062, 0x0026, 0x0016, 0x000e, 0x0005,
	0x0004, 0x0003, 0x0000, 0x0001, 0x000a, 0x0012, 0x0002, 0x0022,
	0x01c6, 0x02c6, 0x06c6, 0x0ec6
};

static const byte fft_level_exp_alt_huffbits[28] = {
	13, 7, 8, 9, 10, 10, 10, 10, 10, 9, 8, 7, 6, 5, 4, 3,
	3, 2, 3, 3, 4, 5, 7, 8, 9, 11, 12, 13
};

// values in this table range from -1..19; adjust retrieved value by -1
static const uint16 fft_level_exp_huffcodes[20] = {
	0x0f24, 0x0001, 0x0002, 0x0000, 0x0006, 0x0005, 0x0007, 0x000c,
	0x000b, 0x0014, 0x0013, 0x0004, 0x0003, 0x0023, 0x0064, 0x00a4,
	0x0024, 0x0124, 0x0324, 0x0724
};

static const byte fft_level_exp_huffbits[20] = {
	12, 3, 3, 3, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 9, 10, 11, 12
};

// values in this table range from -1..6; adjust retrieved value by -1
static const byte fft_stereo_exp_huffcodes[7] = {
	0x3e, 0x01, 0x00, 0x02, 0x06, 0x0e, 0x1e
};

static const byte fft_stereo_exp_huffbits[7] = {
	6, 1, 2, 3, 4, 5, 6
};

// values in this table range from -1..8; adjust retrieved value by -1
static const byte fft_stereo_phase_huffcodes[9] = {
	0x35, 0x02, 0x00, 0x01, 0x0d, 0x15, 0x05, 0x09, 0x03
};

static const byte fft_stereo_phase_huffbits[9] = {
	6, 2, 2, 4, 4, 6, 5, 4, 2
};

static const int fft_cutoff_index_table[4][2] = {
	{ 1, 2 }, {-1, 0 }, {-1,-2 }, { 0, 0 }
};

static const int16 fft_level_index_table[256] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
};

static const byte last_coeff[3] = {
	4, 7, 10
};

static const byte coeff_per_sb_for_avg[3][30] = {
	{ 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
	{ 0, 1, 2, 2, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6 },
	{ 0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9 }
};

static const uint32 dequant_table[3][10][30] = {
	{ { 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 256, 256, 205, 154, 102, 51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 51, 102, 154, 205, 256, 238, 219, 201, 183, 165, 146, 128, 110, 91, 73, 55, 37, 18, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 18, 37, 55, 73, 91, 110, 128, 146, 165, 183, 201, 219, 238, 256, 228, 199, 171, 142, 114, 85, 57, 28 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
	{ { 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 256, 171, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 85, 171, 256, 171, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 85, 171, 256, 219, 183, 146, 110, 73, 37, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 37, 73, 110, 146, 183, 219, 256, 228, 199, 171, 142, 114, 85, 57, 28, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 28, 57, 85, 114, 142, 171, 199, 228, 256, 213, 171, 128, 85, 43 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
	{ { 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 256, 256, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 256, 171, 85, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 85, 171, 256, 192, 128, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 64, 128, 192, 256, 205, 154, 102, 51, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 51, 102, 154, 205, 256, 213, 171, 128, 85, 43, 0, 0, 0, 0, 0, 0 },
	  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 43, 85, 128, 171, 213, 256, 213, 171, 128, 85, 43 } }
};

static const byte coeff_per_sb_for_dequant[3][30] = {
	{ 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3 },
	{ 0, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6 },
	{ 0, 1, 2, 3, 4, 4, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9 }
};

// first index is subband, 2nd index is 0, 1 or 3 (2 is unused)
static const int8 tone_level_idx_offset_table[30][4] = {
	{ -50, -50,  0, -50 },
	{ -50, -50,  0, -50 },
	{ -50,  -9,  0, -19 },
	{ -16,  -6,  0, -12 },
	{ -11,  -4,  0,  -8 },
	{  -8,  -3,  0,  -6 },
	{  -7,  -3,  0,  -5 },
	{  -6,  -2,  0,  -4 },
	{  -5,  -2,  0,  -3 },
	{  -4,  -1,  0,  -3 },
	{  -4,  -1,  0,  -2 },
	{  -3,  -1,  0,  -2 },
	{  -3,  -1,  0,  -2 },
	{  -3,  -1,  0,  -2 },
	{  -2,  -1,  0,  -1 },
	{  -2,  -1,  0,  -1 },
	{  -2,  -1,  0,  -1 },
	{  -2,   0,  0,  -1 },
	{  -2,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,  -1 },
	{  -1,   0,  0,   0 },
	{  -1,   0,  0,   0 },
	{  -1,   0,  0,   0 },
	{  -1,   0,  0,   0 }
};

/* all my samples have 1st index 0 or 1 */
/* second index is subband, only indexes 0-29 seem to be used */
static const int8 coding_method_table[5][30] = {
	{ 34, 30, 24, 24, 16, 16, 16, 16, 10, 10, 10, 10, 10, 10, 10,
	  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10
	},
	{ 34, 30, 24, 24, 16, 16, 16, 16, 10, 10, 10, 10, 10, 10, 10,
	  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10
	},
	{ 34, 30, 30, 30, 24, 24, 16, 16, 16, 16, 16, 16, 10, 10, 10,
	  10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10
	},
	{ 34, 34, 30, 30, 24, 24, 24, 24, 16, 16, 16, 16, 16, 16, 16,
	  16, 16, 16, 16, 16, 16, 16, 10, 10, 10, 10, 10, 10, 10, 10
	},
	{ 34, 34, 30, 30, 30, 30, 30, 30, 24, 24, 24, 24, 24, 24, 24,
	  24, 24, 24, 24, 24, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16
	},
};

static const int vlc_stage3_values[60] = {
	    0,     1,     2,     3,     4,     6,     8,    10,    12,    16,    20,    24,
	   28,    36,    44,    52,    60,    76,    92,   108,   124,   156,   188,   220,
	  252,   316,   380,   444,   508,   636,   764,   892,  1020,  1276,  1532,  1788,
	 2044,  2556,  3068,  3580,  4092,  5116,  6140,  7164,  8188, 10236, 12284, 14332,
	16380, 20476, 24572, 28668, 32764, 40956, 49148, 57340, 65532, 81916, 98300,114684
};

static const float fft_tone_sample_table[4][16][5] = {
	{ { .0100000000f,-.0037037037f,-.0020000000f,-.0069444444f,-.0018416207f },
	  { .0416666667f, .0000000000f, .0000000000f,-.0208333333f,-.0123456791f },
	  { .1250000000f, .0558035709f, .0330687836f,-.0164473690f,-.0097465888f },
	  { .1562500000f, .0625000000f, .0370370370f,-.0062500000f,-.0037037037f },
	  { .1996007860f, .0781250000f, .0462962948f, .0022727272f, .0013468013f },
	  { .2000000000f, .0625000000f, .0370370373f, .0208333333f, .0074074073f },
	  { .2127659619f, .0555555556f, .0329218097f, .0208333333f, .0123456791f },
	  { .2173913121f, .0473484844f, .0280583613f, .0347222239f, .0205761325f },
	  { .2173913121f, .0347222239f, .0205761325f, .0473484844f, .0280583613f },
	  { .2127659619f, .0208333333f, .0123456791f, .0555555556f, .0329218097f },
	  { .2000000000f, .0208333333f, .0074074073f, .0625000000f, .0370370370f },
	  { .1996007860f, .0022727272f, .0013468013f, .0781250000f, .0462962948f },
	  { .1562500000f,-.0062500000f,-.0037037037f, .0625000000f, .0370370370f },
	  { .1250000000f,-.0164473690f,-.0097465888f, .0558035709f, .0330687836f },
	  { .0416666667f,-.0208333333f,-.0123456791f, .0000000000f, .0000000000f },
	  { .0100000000f,-.0069444444f,-.0018416207f,-.0037037037f,-.0020000000f } },

	{ { .0050000000f,-.0200000000f, .0125000000f,-.3030303030f, .0020000000f },
	  { .1041666642f, .0400000000f,-.0250000000f, .0333333333f,-.0200000000f },
	  { .1250000000f, .0100000000f, .0142857144f,-.0500000007f,-.0200000000f },
	  { .1562500000f,-.0006250000f,-.00049382716f,-.000625000f,-.00049382716f },
	  { .1562500000f,-.0006250000f,-.00049382716f,-.000625000f,-.00049382716f },
	  { .1250000000f,-.0500000000f,-.0200000000f, .0100000000f, .0142857144f },
	  { .1041666667f, .0333333333f,-.0200000000f, .0400000000f,-.0250000000f },
	  { .0050000000f,-.3030303030f, .0020000001f,-.0200000000f, .0125000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f } },

	{ { .1428571492f, .1250000000f,-.0285714287f,-.0357142873f, .0208333333f },
	  { .1818181818f, .0588235296f, .0333333333f, .0212765951f, .0100000000f },
	  { .1818181818f, .0212765951f, .0100000000f, .0588235296f, .0333333333f },
	  { .1428571492f,-.0357142873f, .0208333333f, .1250000000f,-.0285714287f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f } },

	{ { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f },
	  { .0000000000f, .0000000000f, .0000000000f, .0000000000f, .0000000000f } }
};

static const float fft_tone_level_table[2][64] = { {
// pow ~ (i > 46) ? 0 : (((((i & 1) ? 431 : 304) << (i >> 1))) / 1024.0);
	0.17677669f, 0.42677650f, 0.60355347f, 0.85355347f,
	1.20710683f, 1.68359375f, 2.37500000f, 3.36718750f,
	4.75000000f, 6.73437500f, 9.50000000f, 13.4687500f,
	19.0000000f, 26.9375000f, 38.0000000f, 53.8750000f,
	76.0000000f, 107.750000f, 152.000000f, 215.500000f,
	304.000000f, 431.000000f, 608.000000f, 862.000000f,
	1216.00000f, 1724.00000f, 2432.00000f, 3448.00000f,
	4864.00000f, 6896.00000f, 9728.00000f, 13792.0000f,
	19456.0000f, 27584.0000f, 38912.0000f, 55168.0000f,
	77824.0000f, 110336.000f, 155648.000f, 220672.000f,
	311296.000f, 441344.000f, 622592.000f, 882688.000f,
	1245184.00f, 1765376.00f, 2490368.00f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
  }, {
// pow = (i > 45) ? 0 : ((((i & 1) ? 431 : 304) << (i >> 1)) / 512.0);
	0.59375000f, 0.84179688f, 1.18750000f, 1.68359375f,
	2.37500000f, 3.36718750f, 4.75000000f, 6.73437500f,
	9.50000000f, 13.4687500f, 19.0000000f, 26.9375000f,
	38.0000000f, 53.8750000f, 76.0000000f, 107.750000f,
	152.000000f, 215.500000f, 304.000000f, 431.000000f,
	608.000000f, 862.000000f, 1216.00000f, 1724.00000f,
	2432.00000f, 3448.00000f, 4864.00000f, 6896.00000f,
	9728.00000f, 13792.0000f, 19456.0000f, 27584.0000f,
	38912.0000f, 55168.0000f, 77824.0000f, 110336.000f,
	155648.000f, 220672.000f, 311296.000f, 441344.000f,
	622592.000f, 882688.000f, 1245184.00f, 1765376.00f,
	2490368.00f, 3530752.00f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f,
	0.00000000f, 0.00000000f, 0.00000000f, 0.00000000f
} };

static const float fft_tone_envelope_table[4][31] = {
	{ .009607375f, .038060248f, .084265202f, .146446645f, .222214907f, .308658302f,
	  .402454883f, .500000060f, .597545207f, .691341758f, .777785182f, .853553414f,
	  .915734828f, .961939812f, .990392685f, 1.00000000f, .990392625f, .961939752f,
	  .915734768f, .853553295f, .777785063f, .691341639f, .597545087f, .500000000f,
	  .402454853f, .308658272f, .222214878f, .146446615f, .084265172f, .038060218f,
	  .009607345f },
	{ .038060248f, .146446645f, .308658302f, .500000060f, .691341758f, .853553414f,
	  .961939812f, 1.00000000f, .961939752f, .853553295f, .691341639f, .500000000f,
	  .308658272f, .146446615f, .038060218f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f },
	{ .146446645f, .500000060f, .853553414f, 1.00000000f, .853553295f, .500000000f,
	  .146446615f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f },
	{ .500000060f, 1.00000000f, .500000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f, .000000000f, .000000000f, .000000000f, .000000000f, .000000000f,
	  .000000000f }
};

static const float sb_noise_attenuation[32] = {
	0.0f, 0.0f, 0.3f, 0.4f, 0.5f, 0.7f, 1.0f, 1.0f,
	1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
	1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
	1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
};

static const byte fft_subpackets[32] = {
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0
};

// first index is joined_stereo, second index is 0 or 2 (1 is unused)
static const float dequant_1bit[2][3] = {
	{-0.920000f, 0.000000f, 0.920000f },
	{-0.890000f, 0.000000f, 0.890000f }
};

static const float type30_dequant[8] = {
	-1.0f,-0.625f,-0.291666656732559f,0.0f,
	0.25f,0.5f,0.75f,1.0f,
};

static const float type34_delta[10] = { // FIXME: covers 8 entries..
	-1.0f,-0.60947573184967f,-0.333333343267441f,-0.138071194291115f,0.0f,
	0.138071194291115f,0.333333343267441f,0.60947573184967f,1.0f,0.0f,
};

} // End of namespace Audio



// ---- ScummVM audio/decoders/qdm2.cpp

// Based off ffmpeg's QDM2 decoder






namespace Audio {

enum {
	SOFTCLIP_THRESHOLD = 27600,
	HARDCLIP_THRESHOLD = 35716,
	MPA_MAX_CHANNELS = 2,
	MPA_FRAME_SIZE = 1152,
	FF_INPUT_BUFFER_PADDING_SIZE = 8
};

typedef int8 sb_int8_array[2][30][64];

struct QDM2SubPacket {
	int type;
	unsigned int size;
	const uint8 *data; // pointer to subpacket data (points to input data buffer, it's not a private copy)
};

struct QDM2SubPNode {
	QDM2SubPacket *packet;
	struct QDM2SubPNode *next; // pointer to next packet in the list, NULL if leaf node
};

struct QDM2Complex {
	float re;
	float im;
};

struct FFTTone {
	float level;
	QDM2Complex *complex;
	const float *table;
	int phase;
	int phase_shift;
	int duration;
	short time_index;
	short cutoff;
};

struct FFTCoefficient {
	int16 sub_packet;
	uint8 channel;
	int16 offset;
	int16 exp;
	uint8 phase;
};

struct VLC {
	int32 bits;
	int16 (*table)[2]; // code, bits
	int32 table_size;
	int32 table_allocated;
};

struct QDM2FFT {
	QDM2Complex complex[MPA_MAX_CHANNELS][256];
} PACKED_STRUCT;

class QDM2Stream : public Codec {
public:
	QDM2Stream(Common::SeekableReadStream *extraData, DisposeAfterUse::Flag disposeExtraData);
	~QDM2Stream();

	AudioStream *decodeFrame(Common::SeekableReadStream &stream);

private:
	// Parameters from codec header, do not change during playback
	uint8 _channels;
	uint16 _sampleRate;
	uint16 _bitRate;
	uint16 _blockSize;  // Group
	uint16 _frameSize;  // FFT
	uint16 _packetSize; // Checksum

	// Parameters built from header parameters, do not change during playback
	int _groupOrder;       // order of frame group
	int _fftOrder;         // order of FFT (actually fft order+1)
	int _fftFrameSize;     // size of fft frame, in components (1 comples = re + im)
	int _sFrameSize;        // size of data frame
	int _frequencyRange;
	int _subSampling;      // subsampling: 0=25%, 1=50%, 2=100% */
	int _coeffPerSbSelect; // selector for "num. of coeffs. per subband" tables. Can be 0, 1, 2
	int _cmTableSelect;    // selector for "coding method" tables. Can be 0, 1 (from init: 0-4)

	// Packets and packet lists
	QDM2SubPacket _subPackets[16];    // the packets themselves
	QDM2SubPNode _subPacketListA[16]; // list of all packets
	QDM2SubPNode _subPacketListB[16]; // FFT packets B are on list
	int _subPacketsB;                 // number of packets on 'B' list
	QDM2SubPNode _subPacketListC[16]; // packets with errors?
	QDM2SubPNode _subPacketListD[16]; // DCT packets

	// FFT and tones
	FFTTone _fftTones[1000];
	int _fftToneStart;
	int _fftToneEnd;
	FFTCoefficient _fftCoefs[1000];
	int _fftCoefsIndex;
	int _fftCoefsMinIndex[5];
	int _fftCoefsMaxIndex[5];
	int _fftLevelExp[6];
	Math::RDFT *_rdft;
	QDM2FFT _fft;

	// I/O data
	uint8 *_compressedData;
	float _outputBuffer[1024];

	// Synthesis filter
	int16 ff_mpa_synth_window[512];
	int16 _synthBuf[MPA_MAX_CHANNELS][512*2];
	int _synthBufOffset[MPA_MAX_CHANNELS];
	int32 _sbSamples[MPA_MAX_CHANNELS][128][32];

	// Mixed temporary data used in decoding
	float _toneLevel[MPA_MAX_CHANNELS][30][64];
	int8 _codingMethod[MPA_MAX_CHANNELS][30][64];
	int8 _quantizedCoeffs[MPA_MAX_CHANNELS][10][8];
	int8 _toneLevelIdxBase[MPA_MAX_CHANNELS][30][8];
	int8 _toneLevelIdxHi1[MPA_MAX_CHANNELS][3][8][8];
	int8 _toneLevelIdxMid[MPA_MAX_CHANNELS][26][8];
	int8 _toneLevelIdxHi2[MPA_MAX_CHANNELS][26];
	int8 _toneLevelIdx[MPA_MAX_CHANNELS][30][64];
	int8 _toneLevelIdxTemp[MPA_MAX_CHANNELS][30][64];

	// Flags
	bool _hasErrors;         // packet has errors
	int _superblocktype_2_3; // select fft tables and some algorithm based on superblock type
	int _doSynthFilter;      // used to perform or skip synthesis filter

	uint8 _subPacket; // 0 to 15
	uint32 _superBlockStart;
	int _noiseIdx; // index for dithering noise table

	byte _emptyBuffer[FF_INPUT_BUFFER_PADDING_SIZE];

	VLC _vlcTabLevel;
	VLC _vlcTabDiff;
	VLC _vlcTabRun;
	VLC _fftLevelExpAltVlc;
	VLC _fftLevelExpVlc;
	VLC _fftStereoExpVlc;
	VLC _fftStereoPhaseVlc;
	VLC _vlcTabToneLevelIdxHi1;
	VLC _vlcTabToneLevelIdxMid;
	VLC _vlcTabToneLevelIdxHi2;
	VLC _vlcTabType30;
	VLC _vlcTabType34;
	VLC _vlcTabFftToneOffset[5];
	bool _vlcsInitialized;
	void initVlc(void);

	uint16 _softclipTable[HARDCLIP_THRESHOLD - SOFTCLIP_THRESHOLD + 1];
	void softclipTableInit(void);

	float _noiseTable[4096];
	byte _randomDequantIndex[256][5];
	byte _randomDequantType24[128][3];
	void rndTableInit(void);

	float _noiseSamples[128];
	void initNoiseSamples(void);

	void average_quantized_coeffs(void);
	void build_sb_samples_from_noise(int sb);
	void fix_coding_method_array(int sb, int channels, sb_int8_array coding_method);
	void fill_tone_level_array(int flag);
	void fill_coding_method_array(sb_int8_array tone_level_idx, sb_int8_array tone_level_idx_temp,
	                              sb_int8_array coding_method, int nb_channels,
	                              int c, int superblocktype_2_3, int cm_table_select);
	void synthfilt_build_sb_samples(Common::BitStreamMemory32LELSB *gb, int length, int sb_min, int sb_max);
	void init_quantized_coeffs_elem0(int8 *quantized_coeffs, Common::BitStreamMemory32LELSB *gb, int length);
	void init_tone_level_dequantization(Common::BitStreamMemory32LELSB *gb, int length);
	void process_subpacket_9(QDM2SubPNode *node);
	void process_subpacket_10(QDM2SubPNode *node, int length);
	void process_subpacket_11(QDM2SubPNode *node, int length);
	void process_subpacket_12(QDM2SubPNode *node, int length);
	void process_synthesis_subpackets(QDM2SubPNode *list);
	void qdm2_decode_super_block(void);
	void qdm2_fft_init_coefficient(int sub_packet, int offset, int duration,
	                               int channel, int exp, int phase);
	void qdm2_fft_decode_tones(int duration, Common::BitStreamMemory32LELSB *gb, int b);
	void qdm2_decode_fft_packets(void);
	void qdm2_fft_generate_tone(FFTTone *tone);
	void qdm2_fft_tone_synthesizer(uint8 sub_packet);
	void qdm2_calculate_fft(int channel);
	void qdm2_synthesis_filter(uint8 index);
	bool qdm2_decodeFrame(Common::SeekableReadStream &in, QueuingAudioStream *audioStream);
};

#define QDM2_LIST_ADD(list, size, packet) \
	do { \
		if (size > 0) \
			list[size - 1].next = &list[size]; \
		list[size].packet = packet; \
		list[size].next = NULL; \
		size++; \
	} while(0)

// Result is 8, 16 or 30
#define QDM2_SB_USED(subSampling) (((subSampling) >= 2) ? 30 : 8 << (subSampling))

#define FIX_NOISE_IDX(noiseIdx) \
	if ((noiseIdx) >= 3840) \
		(noiseIdx) -= 3840 \

#define SB_DITHERING_NOISE(sb, noiseIdx) (_noiseTable[(noiseIdx)++] * sb_noise_attenuation[(sb)])

// half mpeg encoding window (full precision)
const int32 ff_mpa_enwindow[257] = {
	 0,    -1,    -1,    -1,    -1,    -1,    -1,    -2,
	-2,    -2,    -2,    -3,    -3,    -4,    -4,    -5,
	-5,    -6,    -7,    -7,    -8,    -9,   -10,   -11,
   -13,   -14,   -16,   -17,   -19,   -21,   -24,   -26,
   -29,   -31,   -35,   -38,   -41,   -45,   -49,   -53,
   -58,   -63,   -68,   -73,   -79,   -85,   -91,   -97,
  -104,  -111,  -117,  -125,  -132,  -139,  -147,  -154,
  -161,  -169,  -176,  -183,  -190,  -196,  -202,  -208,
   213,   218,   222,   225,   227,   228,   228,   227,
   224,   221,   215,   208,   200,   189,   177,   163,
   146,   127,   106,    83,    57,    29,    -2,   -36,
   -72,  -111,  -153,  -197,  -244,  -294,  -347,  -401,
  -459,  -519,  -581,  -645,  -711,  -779,  -848,  -919,
  -991, -1064, -1137, -1210, -1283, -1356, -1428, -1498,
 -1567, -1634, -1698, -1759, -1817, -1870, -1919, -1962,
 -2001, -2032, -2057, -2075, -2085, -2087, -2080, -2063,
  2037,  2000,  1952,  1893,  1822,  1739,  1644,  1535,
  1414,  1280,  1131,   970,   794,   605,   402,   185,
   -45,  -288,  -545,  -814, -1095, -1388, -1692, -2006,
 -2330, -2663, -3004, -3351, -3705, -4063, -4425, -4788,
 -5153, -5517, -5879, -6237, -6589, -6935, -7271, -7597,
 -7910, -8209, -8491, -8755, -8998, -9219, -9416, -9585,
 -9727, -9838, -9916, -9959, -9966, -9935, -9863, -9750,
 -9592, -9389, -9139, -8840, -8492, -8092, -7640, -7134,
  6574,  5959,  5288,  4561,  3776,  2935,  2037,  1082,
	70,  -998, -2122, -3300, -4533, -5818, -7154, -8540,
 -9975,-11455,-12980,-14548,-16155,-17799,-19478,-21189,
-22929,-24694,-26482,-28289,-30112,-31947,-33791,-35640,
-37489,-39336,-41176,-43006,-44821,-46617,-48390,-50137,
-51853,-53534,-55178,-56778,-58333,-59838,-61289,-62684,
-64019,-65290,-66494,-67629,-68692,-69679,-70590,-71420,
-72169,-72835,-73415,-73908,-74313,-74630,-74856,-74992,
 75038
};

void ff_mpa_synth_init(int16 *window) {
	int i;
	int32 v;

	// max = 18760, max sum over all 16 coefs : 44736
	for(i = 0; i < 257; i++) {
		v = ff_mpa_enwindow[i];
		v = (v + 2) >> 2;
		window[i] = v;

		if ((i & 63) != 0)
			v = -v;

		if (i != 0)
			window[512 - i] = v;
	}
}

static inline uint16 round_sample(int *sum) {
	int sum1;
	sum1 = (*sum) >> 14;
	*sum &= (1 << 14)-1;
	if (sum1 < (-0x7fff - 1))
		sum1 = (-0x7fff - 1);
	if (sum1 > 0x7fff)
		sum1 = 0x7fff;
	return sum1;
}

static inline int MULH(int a, int b) {
	return ((int64)(a) * (int64)(b))>>32;
}

// signed 16x16 -> 32 multiply add accumulate
#define MACS(rt, ra, rb) rt += (ra) * (rb)

#define MLSS(rt, ra, rb) ((rt) -= (ra) * (rb))

#define SUM8(op, sum, w, p)\
{\
	op(sum, (w)[0 * 64], (p)[0 * 64]);\
	op(sum, (w)[1 * 64], (p)[1 * 64]);\
	op(sum, (w)[2 * 64], (p)[2 * 64]);\
	op(sum, (w)[3 * 64], (p)[3 * 64]);\
	op(sum, (w)[4 * 64], (p)[4 * 64]);\
	op(sum, (w)[5 * 64], (p)[5 * 64]);\
	op(sum, (w)[6 * 64], (p)[6 * 64]);\
	op(sum, (w)[7 * 64], (p)[7 * 64]);\
}

#define SUM8P2(sum1, op1, sum2, op2, w1, w2, p) \
{\
	tmp_s = p[0 * 64];\
	op1(sum1, (w1)[0 * 64], tmp_s);\
	op2(sum2, (w2)[0 * 64], tmp_s);\
	tmp_s = p[1 * 64];\
	op1(sum1, (w1)[1 * 64], tmp_s);\
	op2(sum2, (w2)[1 * 64], tmp_s);\
	tmp_s = p[2 * 64];\
	op1(sum1, (w1)[2 * 64], tmp_s);\
	op2(sum2, (w2)[2 * 64], tmp_s);\
	tmp_s = p[3 * 64];\
	op1(sum1, (w1)[3 * 64], tmp_s);\
	op2(sum2, (w2)[3 * 64], tmp_s);\
	tmp_s = p[4 * 64];\
	op1(sum1, (w1)[4 * 64], tmp_s);\
	op2(sum2, (w2)[4 * 64], tmp_s);\
	tmp_s = p[5 * 64];\
	op1(sum1, (w1)[5 * 64], tmp_s);\
	op2(sum2, (w2)[5 * 64], tmp_s);\
	tmp_s = p[6 * 64];\
	op1(sum1, (w1)[6 * 64], tmp_s);\
	op2(sum2, (w2)[6 * 64], tmp_s);\
	tmp_s = p[7 * 64];\
	op1(sum1, (w1)[7 * 64], tmp_s);\
	op2(sum2, (w2)[7 * 64], tmp_s);\
}

#define FIXHR(a) ((int)((a) * (1LL<<32) + 0.5))

// tab[i][j] = 1.0 / (2.0 * cos(pi*(2*k+1) / 2^(6 - j)))

// cos(i*pi/64)

#define COS0_0  FIXHR(0.50060299823519630134/2)
#define COS0_1  FIXHR(0.50547095989754365998/2)
#define COS0_2  FIXHR(0.51544730992262454697/2)
#define COS0_3  FIXHR(0.53104259108978417447/2)
#define COS0_4  FIXHR(0.55310389603444452782/2)
#define COS0_5  FIXHR(0.58293496820613387367/2)
#define COS0_6  FIXHR(0.62250412303566481615/2)
#define COS0_7  FIXHR(0.67480834145500574602/2)
#define COS0_8  FIXHR(0.74453627100229844977/2)
#define COS0_9  FIXHR(0.83934964541552703873/2)
#define COS0_10 FIXHR(0.97256823786196069369/2)
#define COS0_11 FIXHR(1.16943993343288495515/4)
#define COS0_12 FIXHR(1.48416461631416627724/4)
#define COS0_13 FIXHR(2.05778100995341155085/8)
#define COS0_14 FIXHR(3.40760841846871878570/8)
#define COS0_15 FIXHR(10.19000812354805681150/32)

#define COS1_0 FIXHR(0.50241928618815570551/2)
#define COS1_1 FIXHR(0.52249861493968888062/2)
#define COS1_2 FIXHR(0.56694403481635770368/2)
#define COS1_3 FIXHR(0.64682178335999012954/2)
#define COS1_4 FIXHR(0.78815462345125022473/2)
#define COS1_5 FIXHR(1.06067768599034747134/4)
#define COS1_6 FIXHR(1.72244709823833392782/4)
#define COS1_7 FIXHR(5.10114861868916385802/16)

#define COS2_0 FIXHR(0.50979557910415916894/2)
#define COS2_1 FIXHR(0.60134488693504528054/2)
#define COS2_2 FIXHR(0.89997622313641570463/2)
#define COS2_3 FIXHR(2.56291544774150617881/8)

#define COS3_0 FIXHR(0.54119610014619698439/2)
#define COS3_1 FIXHR(1.30656296487637652785/4)

#define COS4_0 FIXHR(0.70710678118654752439/2)

/* butterfly operator */
#define BF(a, b, c, s)\
{\
	tmp0 = tab[a] + tab[b];\
	tmp1 = tab[a] - tab[b];\
	tab[a] = tmp0;\
	tab[b] = MULH(tmp1<<(s), c);\
}

#define BF1(a, b, c, d)\
{\
	BF(a, b, COS4_0, 1);\
	BF(c, d,-COS4_0, 1);\
	tab[c] += tab[d];\
}

#define BF2(a, b, c, d)\
{\
	BF(a, b, COS4_0, 1);\
	BF(c, d,-COS4_0, 1);\
	tab[c] += tab[d];\
	tab[a] += tab[c];\
	tab[c] += tab[b];\
	tab[b] += tab[d];\
}

#define ADD(a, b) tab[a] += tab[b]

// DCT32 without 1/sqrt(2) coef zero scaling.
static void dct32(int32 *out, int32 *tab) {
	int tmp0, tmp1;

	// pass 1
	BF( 0, 31, COS0_0 , 1);
	BF(15, 16, COS0_15, 5);
	// pass 2
	BF( 0, 15, COS1_0 , 1);
	BF(16, 31,-COS1_0 , 1);
	// pass 1
	BF( 7, 24, COS0_7 , 1);
	BF( 8, 23, COS0_8 , 1);
	// pass 2
	BF( 7,  8, COS1_7 , 4);
	BF(23, 24,-COS1_7 , 4);
	// pass 3
	BF( 0,  7, COS2_0 , 1);
	BF( 8, 15,-COS2_0 , 1);
	BF(16, 23, COS2_0 , 1);
	BF(24, 31,-COS2_0 , 1);
	// pass 1
	BF( 3, 28, COS0_3 , 1);
	BF(12, 19, COS0_12, 2);
	// pass 2
	BF( 3, 12, COS1_3 , 1);
	BF(19, 28,-COS1_3 , 1);
	// pass 1
	BF( 4, 27, COS0_4 , 1);
	BF(11, 20, COS0_11, 2);
	// pass 2
	BF( 4, 11, COS1_4 , 1);
	BF(20, 27,-COS1_4 , 1);
	// pass 3
	BF( 3,  4, COS2_3 , 3);
	BF(11, 12,-COS2_3 , 3);
	BF(19, 20, COS2_3 , 3);
	BF(27, 28,-COS2_3 , 3);
	// pass 4
	BF( 0,  3, COS3_0 , 1);
	BF( 4,  7,-COS3_0 , 1);
	BF( 8, 11, COS3_0 , 1);
	BF(12, 15,-COS3_0 , 1);
	BF(16, 19, COS3_0 , 1);
	BF(20, 23,-COS3_0 , 1);
	BF(24, 27, COS3_0 , 1);
	BF(28, 31,-COS3_0 , 1);

	// pass 1
	BF( 1, 30, COS0_1 , 1);
	BF(14, 17, COS0_14, 3);
	// pass 2
	BF( 1, 14, COS1_1 , 1);
	BF(17, 30,-COS1_1 , 1);
	// pass 1
	BF( 6, 25, COS0_6 , 1);
	BF( 9, 22, COS0_9 , 1);
	// pass 2
	BF( 6,  9, COS1_6 , 2);
	BF(22, 25,-COS1_6 , 2);
	// pass 3
	BF( 1,  6, COS2_1 , 1);
	BF( 9, 14,-COS2_1 , 1);
	BF(17, 22, COS2_1 , 1);
	BF(25, 30,-COS2_1 , 1);

	// pass 1
	BF( 2, 29, COS0_2 , 1);
	BF(13, 18, COS0_13, 3);
	// pass 2
	BF( 2, 13, COS1_2 , 1);
	BF(18, 29,-COS1_2 , 1);
	// pass 1
	BF( 5, 26, COS0_5 , 1);
	BF(10, 21, COS0_10, 1);
	// pass 2
	BF( 5, 10, COS1_5 , 2);
	BF(21, 26,-COS1_5 , 2);
	// pass 3
	BF( 2,  5, COS2_2 , 1);
	BF(10, 13,-COS2_2 , 1);
	BF(18, 21, COS2_2 , 1);
	BF(26, 29,-COS2_2 , 1);
	// pass 4
	BF( 1,  2, COS3_1 , 2);
	BF( 5,  6,-COS3_1 , 2);
	BF( 9, 10, COS3_1 , 2);
	BF(13, 14,-COS3_1 , 2);
	BF(17, 18, COS3_1 , 2);
	BF(21, 22,-COS3_1 , 2);
	BF(25, 26, COS3_1 , 2);
	BF(29, 30,-COS3_1 , 2);

	// pass 5
	BF1( 0,  1,  2,  3);
	BF2( 4,  5,  6,  7);
	BF1( 8,  9, 10, 11);
	BF2(12, 13, 14, 15);
	BF1(16, 17, 18, 19);
	BF2(20, 21, 22, 23);
	BF1(24, 25, 26, 27);
	BF2(28, 29, 30, 31);

	// pass 6
	ADD( 8, 12);
	ADD(12, 10);
	ADD(10, 14);
	ADD(14,  9);
	ADD( 9, 13);
	ADD(13, 11);
	ADD(11, 15);

	out[ 0] = tab[0];
	out[16] = tab[1];
	out[ 8] = tab[2];
	out[24] = tab[3];
	out[ 4] = tab[4];
	out[20] = tab[5];
	out[12] = tab[6];
	out[28] = tab[7];
	out[ 2] = tab[8];
	out[18] = tab[9];
	out[10] = tab[10];
	out[26] = tab[11];
	out[ 6] = tab[12];
	out[22] = tab[13];
	out[14] = tab[14];
	out[30] = tab[15];

	ADD(24, 28);
	ADD(28, 26);
	ADD(26, 30);
	ADD(30, 25);
	ADD(25, 29);
	ADD(29, 27);
	ADD(27, 31);

	out[ 1] = tab[16] + tab[24];
	out[17] = tab[17] + tab[25];
	out[ 9] = tab[18] + tab[26];
	out[25] = tab[19] + tab[27];
	out[ 5] = tab[20] + tab[28];
	out[21] = tab[21] + tab[29];
	out[13] = tab[22] + tab[30];
	out[29] = tab[23] + tab[31];
	out[ 3] = tab[24] + tab[20];
	out[19] = tab[25] + tab[21];
	out[11] = tab[26] + tab[22];
	out[27] = tab[27] + tab[23];
	out[ 7] = tab[28] + tab[18];
	out[23] = tab[29] + tab[19];
	out[15] = tab[30] + tab[17];
	out[31] = tab[31];
}

// 32 sub band synthesis filter. Input: 32 sub band samples, Output:
// 32 samples.
// XXX: optimize by avoiding ring buffer usage
void ff_mpa_synth_filter(int16 *synth_buf_ptr, int *synth_buf_offset,
						 int16 *window, int *dither_state,
						 int16 *samples, int incr,
						 int32 sb_samples[32])
{
	int16 *synth_buf;
	const int16 *w, *w2, *p;
	int j, offset;
	int16 *samples2;
	int32 tmp[32];
	int sum, sum2;
	int tmp_s;

	offset = *synth_buf_offset;
	synth_buf = synth_buf_ptr + offset;

	dct32(tmp, sb_samples);
	for(j = 0; j < 32; j++) {
		// NOTE: can cause a loss in precision if very high amplitude sound
		if (tmp[j] < (-0x7fff - 1))
			synth_buf[j] = (-0x7fff - 1);
		else if (tmp[j] > 0x7fff)
			synth_buf[j] = 0x7fff;
		else
			synth_buf[j] = tmp[j];
	}

	// copy to avoid wrap
	memcpy(synth_buf + 512, synth_buf, 32 * sizeof(int16));

	samples2 = samples + 31 * incr;
	w = window;
	w2 = window + 31;

	sum = *dither_state;
	p = synth_buf + 16;
	SUM8(MACS, sum, w, p);
	p = synth_buf + 48;
	SUM8(MLSS, sum, w + 32, p);
	*samples = round_sample(&sum);
	samples += incr;
	w++;

	// we calculate two samples at the same time to avoid one memory
	// access per two sample
	for(j = 1; j < 16; j++) {
		sum2 = 0;
		p = synth_buf + 16 + j;
		SUM8P2(sum, MACS, sum2, MLSS, w, w2, p);
		p = synth_buf + 48 - j;
		SUM8P2(sum, MLSS, sum2, MLSS, w + 32, w2 + 32, p);

		*samples = round_sample(&sum);
		samples += incr;
		sum += sum2;
		*samples2 = round_sample(&sum);
		samples2 -= incr;
		w++;
		w2--;
	}

	p = synth_buf + 32;
	SUM8(MLSS, sum, w + 32, p);
	*samples = round_sample(&sum);
	*dither_state= sum;

	offset = (offset - 32) & 511;
	*synth_buf_offset = offset;
}

/**
 * parses a vlc code, faster then get_vlc()
 * @param bits is the number of bits which will be read at once, must be
 *             identical to nb_bits in init_vlc()
 * @param max_depth is the number of times bits bits must be read to completely
 *                  read the longest vlc code
 *                  = (max_vlc_length + bits - 1) / bits
 */
static int getVlc2(Common::BitStreamMemory32LELSB *s, int16 (*table)[2], int bits, int maxDepth) {
	int index = s->peekBits(bits);
	int code = table[index][0];
	int n = table[index][1];

	if (maxDepth > 1 && n < 0) {
		s->skip(bits);
		int nbBits = -n;
		index = s->peekBits(-n) + code;
		code = table[index][0];
		n = table[index][1];

		if (maxDepth > 2 && n < 0) {
			s->skip(nbBits);
			index = s->getBits(-n) + code;
			code = table[index][0];
			n = table[index][1];
		}
	}

	s->skip(n);
	return code;
}

static int allocTable(VLC *vlc, int size, int use_static) {
	int index;
	int16 (*temp)[2] = NULL;
	index = vlc->table_size;
	vlc->table_size += size;
	if (vlc->table_size > vlc->table_allocated) {
		if(use_static)
			error("QDM2 can't do anything, init_vlc() is used with too little memory");
		vlc->table_allocated += (1 << vlc->bits);
		temp = (int16 (*)[2])realloc(vlc->table, sizeof(int16 *) * 2 * vlc->table_allocated);
		if (!temp) {
			free(vlc->table);
			vlc->table = NULL;
			return -1;
		}
		vlc->table = temp;
	}
	return index;
}

#define GET_DATA(v, table, i, wrap, size)\
{\
	const uint8 *ptr = (const uint8 *)table + i * wrap;\
	switch(size) {\
		case 1:\
			v = *(const uint8 *)ptr;\
			break;\
		case 2:\
			v = *(const uint16 *)ptr;\
			break;\
		default:\
			v = *(const uint32 *)ptr;\
			break;\
	}\
}

static int build_table(VLC *vlc, int table_nb_bits,
					   int nb_codes,
					   const void *bits, int bits_wrap, int bits_size,
					   const void *codes, int codes_wrap, int codes_size,
					   const void *symbols, int symbols_wrap, int symbols_size,
					   int code_prefix, int n_prefix, int flags)
{
	int i, j, k, n, table_size, table_index, nb, n1, index, code_prefix2, symbol;
	uint32 code;
	int16 (*table)[2];

	table_size = 1 << table_nb_bits;
	table_index = allocTable(vlc, table_size, flags & 4);
	if (table_index < 0)
		return -1;
	table = &vlc->table[table_index];

	for(i = 0; i < table_size; i++) {
		table[i][1] = 0; //bits
		table[i][0] = -1; //codes
	}

	// first pass: map codes and compute auxiliary table sizes
	for(i = 0; i < nb_codes; i++) {
		GET_DATA(n, bits, i, bits_wrap, bits_size);
		GET_DATA(code, codes, i, codes_wrap, codes_size);
		// we accept tables with holes
		if (n <= 0)
			continue;
		if (!symbols)
			symbol = i;
		else
			GET_DATA(symbol, symbols, i, symbols_wrap, symbols_size);
		// if code matches the prefix, it is in the table
		n -= n_prefix;
		if(flags & 2)
			code_prefix2= code & (n_prefix>=32 ? 0xffffffff : (1 << n_prefix)-1);
		else
			code_prefix2= code >> n;
		if (n > 0 && code_prefix2 == code_prefix) {
			if (n <= table_nb_bits) {
				// no need to add another table
				j = (code << (table_nb_bits - n)) & (table_size - 1);
				nb = 1 << (table_nb_bits - n);
				for(k = 0; k < nb; k++) {
					if(flags & 2)
						j = (code >> n_prefix) + (k<<n);
					if (table[j][1] /*bits*/ != 0) {
						error("QDM2 incorrect codes");
						return -1;
					}
					table[j][1] = n; //bits
					table[j][0] = symbol;
					j++;
				}
			} else {
				n -= table_nb_bits;
				j = (code >> ((flags & 2) ? n_prefix : n)) & ((1 << table_nb_bits) - 1);
				// compute table size
				n1 = -table[j][1]; //bits
				if (n > n1)
					n1 = n;
				table[j][1] = -n1; //bits
			}
		}
	}

	// second pass : fill auxiliary tables recursively
	for(i = 0;i < table_size; i++) {
		n = table[i][1]; //bits
		if (n < 0) {
			n = -n;
			if (n > table_nb_bits) {
				n = table_nb_bits;
				table[i][1] = -n; //bits
			}
			index = build_table(vlc, n, nb_codes,
			                    bits, bits_wrap, bits_size,
			                    codes, codes_wrap, codes_size,
			                    symbols, symbols_wrap, symbols_size,
			                    (flags & 2) ? (code_prefix | (i << n_prefix)) : ((code_prefix << table_nb_bits) | i),
			                    n_prefix + table_nb_bits, flags);
 			if (index < 0)
				return -1;
			// note: realloc has been done, so reload tables
			table = &vlc->table[table_index];
			table[i][0] = index; //code
		}
	}
	return table_index;
}

/* Build VLC decoding tables suitable for use with get_vlc().

   'nb_bits' set thee decoding table size (2^nb_bits) entries. The
   bigger it is, the faster is the decoding. But it should not be too
   big to save memory and L1 cache. '9' is a good compromise.

   'nb_codes' : number of vlcs codes

   'bits' : table which gives the size (in bits) of each vlc code.

   'codes' : table which gives the bit pattern of of each vlc code.

   'symbols' : table which gives the values to be returned from get_vlc().

   'xxx_wrap' : give the number of bytes between each entry of the
   'bits' or 'codes' tables.

   'xxx_size' : gives the number of bytes of each entry of the 'bits'
   or 'codes' tables.

   'wrap' and 'size' allows to use any memory configuration and types
   (byte/word/long) to store the 'bits', 'codes', and 'symbols' tables.

   'use_static' should be set to 1 for tables, which should be freed
   with av_free_static(), 0 if free_vlc() will be used.
*/
void initVlcSparse(VLC *vlc, int nb_bits, int nb_codes,
		const void *bits, int bits_wrap, int bits_size,
		const void *codes, int codes_wrap, int codes_size,
		const void *symbols, int symbols_wrap, int symbols_size) {
	vlc->bits = nb_bits;

	if (vlc->table_size && vlc->table_size == vlc->table_allocated) {
		return;
	} else if (vlc->table_size) {
		error("called on a partially initialized table");
	}

	if (build_table(vlc, nb_bits, nb_codes,
	                bits, bits_wrap, bits_size,
	                codes, codes_wrap, codes_size,
	                symbols, symbols_wrap, symbols_size,
	                0, 0, 4 | 2) < 0) {
		free(vlc->table);
		return; // Error
	}

	if(vlc->table_size != vlc->table_allocated)
		error("QDM2 needed %d had %d", vlc->table_size, vlc->table_allocated);
}

void QDM2Stream::softclipTableInit(void) {
	uint16 i;
	double dfl = SOFTCLIP_THRESHOLD - 32767;
	float delta = 1.0 / -dfl;

	for (i = 0; i < ARRAYSIZE(_softclipTable); i++)
		_softclipTable[i] = SOFTCLIP_THRESHOLD - ((int)(sin((float)i * delta) * dfl) & 0x0000FFFF);
}

// random generated table
void QDM2Stream::rndTableInit(void) {
	uint16 i;
	uint16 j;
	uint32 ldw, hdw;
	int64 tmp64_1;
	int64 random_seed = 0;
	float delta = 1.0 / 16384.0;

	for(i = 0; i < ARRAYSIZE(_noiseTable); i++) {
		random_seed = random_seed * 214013 + 2531011;
		_noiseTable[i] = (delta * (float)(((int32)random_seed >> 16) & 0x00007FFF)- 1.0) * 1.3;
	}

	for (i = 0; i < 256; i++) {
		random_seed = 81;
		ldw = i;
		for (j = 0; j < 5; j++) {
			_randomDequantIndex[i][j] = (uint8)((ldw / random_seed) & 0xFF);
			ldw = (uint32)ldw % (uint32)random_seed;
			tmp64_1 = (random_seed * 0x55555556);
			hdw = (uint32)(tmp64_1 >> 32);
			random_seed = (int64)(hdw + (ldw >> 31));
		}
	}

	for (i = 0; i < 128; i++) {
		random_seed = 25;
		ldw = i;
		for (j = 0; j < 3; j++) {
			_randomDequantType24[i][j] = (uint8)((ldw / random_seed) & 0xFF);
			ldw = (uint32)ldw % (uint32)random_seed;
			tmp64_1 = (random_seed * 0x66666667);
			hdw = (uint32)(tmp64_1 >> 33);
			random_seed = hdw + (ldw >> 31);
		}
	}
}

void QDM2Stream::initNoiseSamples(void) {
	uint16 i;
	uint32 random_seed = 0;
	float delta = 1.0 / 16384.0;

	for (i = 0; i < ARRAYSIZE(_noiseSamples); i++) {
		random_seed = random_seed * 214013 + 2531011;
		_noiseSamples[i] = (delta * (float)((random_seed >> 16) & 0x00007fff) - 1.0);
	}
}

static const uint16 qdm2_vlc_offs[18] = {
	0, 260, 566, 598, 894, 1166, 1230, 1294, 1678, 1950, 2214, 2278, 2310, 2570, 2834, 3124, 3448, 3838
};

void QDM2Stream::initVlc(void) {
	static int16 qdm2_table[3838][2];

	if (!_vlcsInitialized) {
		_vlcTabLevel.table = &qdm2_table[qdm2_vlc_offs[0]];
		_vlcTabLevel.table_allocated = qdm2_vlc_offs[1] - qdm2_vlc_offs[0];
		_vlcTabLevel.table_size = 0;
		initVlcSparse(&_vlcTabLevel, 8, 24,
			vlc_tab_level_huffbits, 1, 1,
			vlc_tab_level_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabDiff.table = &qdm2_table[qdm2_vlc_offs[1]];
		_vlcTabDiff.table_allocated = qdm2_vlc_offs[2] - qdm2_vlc_offs[1];
		_vlcTabDiff.table_size = 0;
		initVlcSparse(&_vlcTabDiff, 8, 37,
			vlc_tab_diff_huffbits, 1, 1,
			vlc_tab_diff_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabRun.table = &qdm2_table[qdm2_vlc_offs[2]];
		_vlcTabRun.table_allocated = qdm2_vlc_offs[3] - qdm2_vlc_offs[2];
		_vlcTabRun.table_size = 0;
		initVlcSparse(&_vlcTabRun, 5, 6,
			vlc_tab_run_huffbits, 1, 1,
			vlc_tab_run_huffcodes, 1, 1, NULL, 0, 0);

		_fftLevelExpAltVlc.table = &qdm2_table[qdm2_vlc_offs[3]];
		_fftLevelExpAltVlc.table_allocated = qdm2_vlc_offs[4] - qdm2_vlc_offs[3];
		_fftLevelExpAltVlc.table_size = 0;
		initVlcSparse(&_fftLevelExpAltVlc, 8, 28,
			fft_level_exp_alt_huffbits, 1, 1,
			fft_level_exp_alt_huffcodes, 2, 2, NULL, 0, 0);

		_fftLevelExpVlc.table = &qdm2_table[qdm2_vlc_offs[4]];
		_fftLevelExpVlc.table_allocated = qdm2_vlc_offs[5] - qdm2_vlc_offs[4];
		_fftLevelExpVlc.table_size = 0;
		initVlcSparse(&_fftLevelExpVlc, 8, 20,
			fft_level_exp_huffbits, 1, 1,
			fft_level_exp_huffcodes, 2, 2, NULL, 0, 0);

		_fftStereoExpVlc.table = &qdm2_table[qdm2_vlc_offs[5]];
		_fftStereoExpVlc.table_allocated = qdm2_vlc_offs[6] - qdm2_vlc_offs[5];
		_fftStereoExpVlc.table_size = 0;
		initVlcSparse(&_fftStereoExpVlc, 6, 7,
			fft_stereo_exp_huffbits, 1, 1,
			fft_stereo_exp_huffcodes, 1, 1, NULL, 0, 0);

		_fftStereoPhaseVlc.table = &qdm2_table[qdm2_vlc_offs[6]];
		_fftStereoPhaseVlc.table_allocated = qdm2_vlc_offs[7] - qdm2_vlc_offs[6];
		_fftStereoPhaseVlc.table_size = 0;
		initVlcSparse(&_fftStereoPhaseVlc, 6, 9,
			fft_stereo_phase_huffbits, 1, 1,
			fft_stereo_phase_huffcodes, 1, 1, NULL, 0, 0);

		_vlcTabToneLevelIdxHi1.table = &qdm2_table[qdm2_vlc_offs[7]];
		_vlcTabToneLevelIdxHi1.table_allocated = qdm2_vlc_offs[8] - qdm2_vlc_offs[7];
		_vlcTabToneLevelIdxHi1.table_size = 0;
		initVlcSparse(&_vlcTabToneLevelIdxHi1, 8, 20,
			vlc_tab_tone_level_idx_hi1_huffbits, 1, 1,
			vlc_tab_tone_level_idx_hi1_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabToneLevelIdxMid.table = &qdm2_table[qdm2_vlc_offs[8]];
		_vlcTabToneLevelIdxMid.table_allocated = qdm2_vlc_offs[9] - qdm2_vlc_offs[8];
		_vlcTabToneLevelIdxMid.table_size = 0;
		initVlcSparse(&_vlcTabToneLevelIdxMid, 8, 24,
			vlc_tab_tone_level_idx_mid_huffbits, 1, 1,
			vlc_tab_tone_level_idx_mid_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabToneLevelIdxHi2.table = &qdm2_table[qdm2_vlc_offs[9]];
		_vlcTabToneLevelIdxHi2.table_allocated = qdm2_vlc_offs[10] - qdm2_vlc_offs[9];
		_vlcTabToneLevelIdxHi2.table_size = 0;
		initVlcSparse(&_vlcTabToneLevelIdxHi2, 8, 24,
			vlc_tab_tone_level_idx_hi2_huffbits, 1, 1,
			vlc_tab_tone_level_idx_hi2_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabType30.table = &qdm2_table[qdm2_vlc_offs[10]];
		_vlcTabType30.table_allocated = qdm2_vlc_offs[11] - qdm2_vlc_offs[10];
		_vlcTabType30.table_size = 0;
		initVlcSparse(&_vlcTabType30, 6, 9,
			vlc_tab_type30_huffbits, 1, 1,
			vlc_tab_type30_huffcodes, 1, 1, NULL, 0, 0);

		_vlcTabType34.table = &qdm2_table[qdm2_vlc_offs[11]];
		_vlcTabType34.table_allocated = qdm2_vlc_offs[12] - qdm2_vlc_offs[11];
		_vlcTabType34.table_size = 0;
		initVlcSparse(&_vlcTabType34, 5, 10,
			vlc_tab_type34_huffbits, 1, 1,
			vlc_tab_type34_huffcodes, 1, 1, NULL, 0, 0);

		_vlcTabFftToneOffset[0].table = &qdm2_table[qdm2_vlc_offs[12]];
		_vlcTabFftToneOffset[0].table_allocated = qdm2_vlc_offs[13] - qdm2_vlc_offs[12];
		_vlcTabFftToneOffset[0].table_size = 0;
		initVlcSparse(&_vlcTabFftToneOffset[0], 8, 23,
			vlc_tab_fft_tone_offset_0_huffbits, 1, 1,
			vlc_tab_fft_tone_offset_0_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabFftToneOffset[1].table = &qdm2_table[qdm2_vlc_offs[13]];
		_vlcTabFftToneOffset[1].table_allocated = qdm2_vlc_offs[14] - qdm2_vlc_offs[13];
		_vlcTabFftToneOffset[1].table_size = 0;
		initVlcSparse(&_vlcTabFftToneOffset[1], 8, 28,
			vlc_tab_fft_tone_offset_1_huffbits, 1, 1,
			vlc_tab_fft_tone_offset_1_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabFftToneOffset[2].table = &qdm2_table[qdm2_vlc_offs[14]];
		_vlcTabFftToneOffset[2].table_allocated = qdm2_vlc_offs[15] - qdm2_vlc_offs[14];
		_vlcTabFftToneOffset[2].table_size = 0;
		initVlcSparse(&_vlcTabFftToneOffset[2], 8, 32,
			vlc_tab_fft_tone_offset_2_huffbits, 1, 1,
			vlc_tab_fft_tone_offset_2_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabFftToneOffset[3].table = &qdm2_table[qdm2_vlc_offs[15]];
		_vlcTabFftToneOffset[3].table_allocated = qdm2_vlc_offs[16] - qdm2_vlc_offs[15];
		_vlcTabFftToneOffset[3].table_size = 0;
		initVlcSparse(&_vlcTabFftToneOffset[3], 8, 35,
			vlc_tab_fft_tone_offset_3_huffbits, 1, 1,
			vlc_tab_fft_tone_offset_3_huffcodes, 2, 2, NULL, 0, 0);

		_vlcTabFftToneOffset[4].table = &qdm2_table[qdm2_vlc_offs[16]];
		_vlcTabFftToneOffset[4].table_allocated = qdm2_vlc_offs[17] - qdm2_vlc_offs[16];
		_vlcTabFftToneOffset[4].table_size = 0;
		initVlcSparse(&_vlcTabFftToneOffset[4], 8, 38,
			vlc_tab_fft_tone_offset_4_huffbits, 1, 1,
			vlc_tab_fft_tone_offset_4_huffcodes, 2, 2, NULL, 0, 0);

		_vlcsInitialized = true;
	}
}

QDM2Stream::QDM2Stream(Common::SeekableReadStream *extraData, DisposeAfterUse::Flag disposeExtraData) {
	uint32 tmp;
	int tmp_val;
	int i;

	debug(1, "QDM2Stream::QDM2Stream() Call");

	_compressedData = NULL;
	_subPacket = 0;
	_superBlockStart = 0;
	memset(_quantizedCoeffs, 0, sizeof(_quantizedCoeffs));
	memset(_fftLevelExp, 0, sizeof(_fftLevelExp));
	_noiseIdx = 0;
	memset(_fftCoefsMinIndex, 0, sizeof(_fftCoefsMinIndex));
	memset(_fftCoefsMaxIndex, 0, sizeof(_fftCoefsMaxIndex));
	_fftToneStart = 0;
	_fftToneEnd = 0;
	for(i = 0; i < ARRAYSIZE(_subPacketListA); i++) {
		_subPacketListA[i].packet = NULL;
		_subPacketListA[i].next = NULL;
	}
	_subPacketsB = 0;
	for(i = 0; i < ARRAYSIZE(_subPacketListB); i++) {
		_subPacketListB[i].packet = NULL;
		_subPacketListB[i].next = NULL;
	}
	for(i = 0; i < ARRAYSIZE(_subPacketListC); i++) {
		_subPacketListC[i].packet = NULL;
		_subPacketListC[i].next = NULL;
	}
	for(i = 0; i < ARRAYSIZE(_subPacketListD); i++) {
		_subPacketListD[i].packet = NULL;
		_subPacketListD[i].next = NULL;
	}
	memset(_synthBuf, 0, sizeof(_synthBuf));
	memset(_synthBufOffset, 0, sizeof(_synthBufOffset));
	memset(_sbSamples, 0, sizeof(_sbSamples));
	memset(_outputBuffer, 0, sizeof(_outputBuffer));
	_vlcsInitialized = false;
	_superblocktype_2_3 = 0;
	_hasErrors = false;

	// The QDM2 "extra data" is really just an amalgam of three QuickTime
	// atoms needed to correctly set up the decoder.

	// Rewind extraData stream from any previous calls
	extraData->seek(0, SEEK_SET);

	// First, the frma atom
	uint32 frmaSize = extraData->readUint32BE();
	if (frmaSize != 12)
		error("Invalid QDM2 frma atom");

	if (extraData->readUint32BE() != MKTAG('f', 'r', 'm', 'a'))
		error("Failed to find frma atom for QDM2");

	uint32 version = extraData->readUint32BE();
	if (version == MKTAG('Q', 'D', 'M', 'C'))
		error("Unhandled QDMC sound");
	else if (version != MKTAG('Q', 'D', 'M', '2'))
		error("Failed to find QDM2 tag in frma atom");

	// Second, the QDCA atom
	uint32 qdcaSize = extraData->readUint32BE();
	if (qdcaSize > (uint32)(extraData->size() - extraData->pos()))
		error("Invalid QDM2 QDCA atom");

	if (extraData->readUint32BE() != MKTAG('Q', 'D', 'C', 'A'))
		error("Failed to find QDCA atom for QDM2");

	extraData->readUint32BE(); // unknown

	_channels = extraData->readUint32BE();
	_sampleRate = extraData->readUint32BE();
	_bitRate = extraData->readUint32BE();
	_blockSize = extraData->readUint32BE();
	_frameSize = extraData->readUint32BE();
	_packetSize = extraData->readUint32BE();

	// Third, we don't care about the QDCP atom

	_fftOrder = Common::intLog2(_frameSize) + 1;
	_fftFrameSize = 2 * _frameSize; // complex has two floats

	// something like max decodable tones
	_groupOrder = Common::intLog2(_blockSize) + 1;
	_sFrameSize = _blockSize / 16; // 16 iterations per super block

	_subSampling = _fftOrder - 7;
	_frequencyRange = 255 / (1 << (2 - _subSampling));

	switch (_subSampling * 2 + _channels - 1) {
		case 0:
			tmp = 40;
			break;
		case 1:
			tmp = 48;
			break;
		case 2:
			tmp = 56;
			break;
		case 3:
			tmp = 72;
			break;
		case 4:
			tmp = 80;
			break;
		case 5:
			tmp = 100;
			break;
		default:
			tmp = _subSampling;
			break;
	}

	tmp_val = 0;
	if ((tmp * 1000) < _bitRate)  tmp_val = 1;
	if ((tmp * 1440) < _bitRate)  tmp_val = 2;
	if ((tmp * 1760) < _bitRate)  tmp_val = 3;
	if ((tmp * 2240) < _bitRate)  tmp_val = 4;
	_cmTableSelect = tmp_val;

	if (_subSampling == 0)
		tmp = 7999;
	else
		tmp = ((-(_subSampling -1)) & 8000) + 20000;

	if (tmp < 8000)
		_coeffPerSbSelect = 0;
	else if (tmp <= 16000)
		_coeffPerSbSelect = 1;
	else
		_coeffPerSbSelect = 2;

	if (_fftOrder < 7 || _fftOrder > 9)
		error("QDM2Stream::QDM2Stream() Unsupported fft_order: %d", _fftOrder);

	_rdft = new Math::RDFT(_fftOrder, Math::RDFT::IDFT_C2R);

	initVlc();
	ff_mpa_synth_init(ff_mpa_synth_window);
	softclipTableInit();
	rndTableInit();
	initNoiseSamples();

	_compressedData = new uint8[_packetSize + FF_INPUT_BUFFER_PADDING_SIZE];

	if (disposeExtraData == DisposeAfterUse::YES)
		delete extraData;
}

QDM2Stream::~QDM2Stream() {
	delete _rdft;
	delete[] _compressedData;
}

static int qdm2_get_vlc(Common::BitStreamMemory32LELSB *gb, VLC *vlc, int flag, int depth) {
	int value = getVlc2(gb, vlc->table, vlc->bits, depth);

	// stage-2, 3 bits exponent escape sequence
	if (value-- == 0)
		value = gb->getBits(gb->getBits<3>() + 1);

	// stage-3, optional
	if (flag) {
		int tmp = vlc_stage3_values[value];

		if ((value & ~3) > 0)
			tmp += gb->getBits(value >> 2);
		value = tmp;
	}

	return value;
}

static int qdm2_get_se_vlc(VLC *vlc, Common::BitStreamMemory32LELSB *gb, int depth)
{
	int value = qdm2_get_vlc(gb, vlc, 0, depth);

	return (value & 1) ? ((value + 1) >> 1) : -(value >> 1);
}

/**
 * QDM2 checksum
 *
 * @param data      pointer to data to be checksum'ed
 * @param length    data length
 * @param value     checksum value
 *
 * @return          0 if checksum is OK
 */
static uint16 qdm2_packet_checksum(const uint8 *data, int length, int value) {
	int i;

	for (i = 0; i < length; i++)
		value -= data[i];

	return (uint16)(value & 0xffff);
}

/**
 * Return node pointer to first packet of requested type in list.
 *
 * @param list    list of subpackets to be scanned
 * @param type    type of searched subpacket
 * @return        node pointer for subpacket if found, else NULL
 */
static QDM2SubPNode* qdm2_search_subpacket_type_in_list(QDM2SubPNode *list, int type)
{
	while (list != NULL && list->packet != NULL) {
		if (list->packet->type == type)
			return list;
		list = list->next;
	}
	return NULL;
}

/**
 * Replaces 8 elements with their average value.
 * Called by qdm2_decode_superblock before starting subblock decoding.
 */
void QDM2Stream::average_quantized_coeffs(void) {
	int i, j, n, ch, sum;

	n = coeff_per_sb_for_avg[_coeffPerSbSelect][QDM2_SB_USED(_subSampling) - 1] + 1;

	for (ch = 0; ch < _channels; ch++) {
		for (i = 0; i < n; i++) {
			sum = 0;

			for (j = 0; j < 8; j++)
				sum += _quantizedCoeffs[ch][i][j];

			sum /= 8;
			if (sum > 0)
				sum--;

			for (j = 0; j < 8; j++)
				_quantizedCoeffs[ch][i][j] = sum;
		}
	}
}

/**
 * Build subband samples with noise weighted by q->tone_level.
 * Called by synthfilt_build_sb_samples.
 *
 * @param sb    subband index
 */
void QDM2Stream::build_sb_samples_from_noise(int sb) {
	int ch, j;

	FIX_NOISE_IDX(_noiseIdx);

	if (!_channels)
		return;

	for (ch = 0; ch < _channels; ch++) {
		for (j = 0; j < 64; j++) {
			_sbSamples[ch][j * 2][sb] = (int32)(SB_DITHERING_NOISE(sb, _noiseIdx) * _toneLevel[ch][sb][j] + .5);
			_sbSamples[ch][j * 2 + 1][sb] = (int32)(SB_DITHERING_NOISE(sb, _noiseIdx) * _toneLevel[ch][sb][j] + .5);
		}
	}
}

/**
 * Called while processing data from subpackets 11 and 12.
 * Used after making changes to coding_method array.
 *
 * @param sb               subband index
 * @param channels         number of channels
 * @param coding_method    q->coding_method[0][0][0]
 */
void QDM2Stream::fix_coding_method_array(int sb, int channels, sb_int8_array coding_method)
{
	int j, k;
	int ch;
	int run, case_val;
	int switchtable[23] = {0,5,1,5,5,5,5,5,2,5,5,5,5,5,5,5,3,5,5,5,5,5,4};

	for (ch = 0; ch < channels; ch++) {
		for (j = 0; j < 64; ) {
			if ((coding_method[ch][sb][j] - 8) > 22) {
				run = 1;
				case_val = 8;
			} else {
				switch (switchtable[coding_method[ch][sb][j]-8]) {
					case 0: run = 10; case_val = 10; break;
					case 1: run = 1; case_val = 16; break;
					case 2: run = 5; case_val = 24; break;
					case 3: run = 3; case_val = 30; break;
					case 4: run = 1; case_val = 30; break;
					case 5: run = 1; case_val = 8; break;
					default: run = 1; case_val = 8; break;
				}
			}
			for (k = 0; k < run; k++)
				if (j + k < 128)
					if (coding_method[ch][sb + (j + k) / 64][(j + k) % 64] > coding_method[ch][sb][j])
						if (k > 0) {
							warning("QDM2 Untested Code: not debugged, almost never used");
							memset(&coding_method[ch][sb][j + k], case_val, k * sizeof(int8));
							memset(&coding_method[ch][sb][j + k], case_val, 3 * sizeof(int8));
						}
			j += run;
		}
	}
}

/**
 * Related to synthesis filter
 * Called by process_subpacket_10
 *
 * @param flag    1 if called after getting data from subpacket 10, 0 if no subpacket 10
 */
void QDM2Stream::fill_tone_level_array(int flag) {
	int i, sb, ch, sb_used;
	int tmp, tab;

	// This should never happen
	if (_channels <= 0)
		return;

	for (ch = 0; ch < _channels; ch++) {
		for (sb = 0; sb < 30; sb++) {
			for (i = 0; i < 8; i++) {
				if ((tab=coeff_per_sb_for_dequant[_coeffPerSbSelect][sb]) < (last_coeff[_coeffPerSbSelect] - 1))
					tmp = _quantizedCoeffs[ch][tab + 1][i] * dequant_table[_coeffPerSbSelect][tab + 1][sb]+
					      _quantizedCoeffs[ch][tab][i] * dequant_table[_coeffPerSbSelect][tab][sb];
				else
					tmp = _quantizedCoeffs[ch][tab][i] * dequant_table[_coeffPerSbSelect][tab][sb];
				if(tmp < 0)
					tmp += 0xff;
				_toneLevelIdxBase[ch][sb][i] = (tmp / 256) & 0xff;
			}
		}
	}

	sb_used = QDM2_SB_USED(_subSampling);

	if ((_superblocktype_2_3 != 0) && !flag) {
		for (sb = 0; sb < sb_used; sb++) {
			for (ch = 0; ch < _channels; ch++) {
				for (i = 0; i < 64; i++) {
					_toneLevelIdx[ch][sb][i] = _toneLevelIdxBase[ch][sb][i / 8];
					if (_toneLevelIdx[ch][sb][i] < 0)
						_toneLevel[ch][sb][i] = 0;
					else
						_toneLevel[ch][sb][i] = fft_tone_level_table[0][_toneLevelIdx[ch][sb][i] & 0x3f];
				}
			}
		}
	} else {
		tab = _superblocktype_2_3 ? 0 : 1;
		for (sb = 0; sb < sb_used; sb++) {
			if ((sb >= 4) && (sb <= 23)) {
				for (ch = 0; ch < _channels; ch++) {
					for (i = 0; i < 64; i++) {
						tmp = _toneLevelIdxBase[ch][sb][i / 8] -
						      _toneLevelIdxHi1[ch][sb / 8][i / 8][i % 8] -
						      _toneLevelIdxMid[ch][sb - 4][i / 8] -
						      _toneLevelIdxHi2[ch][sb - 4];
						_toneLevelIdx[ch][sb][i] = tmp & 0xff;
						if ((tmp < 0) || (!_superblocktype_2_3 && !tmp))
							_toneLevel[ch][sb][i] = 0;
						else
							_toneLevel[ch][sb][i] = fft_tone_level_table[tab][tmp & 0x3f];
					}
				}
			} else {
				if (sb > 4) {
					for (ch = 0; ch < _channels; ch++) {
						for (i = 0; i < 64; i++) {
							tmp = _toneLevelIdxBase[ch][sb][i / 8] -
							      _toneLevelIdxHi1[ch][2][i / 8][i % 8] -
							      _toneLevelIdxHi2[ch][sb - 4];
							_toneLevelIdx[ch][sb][i] = tmp & 0xff;
							if ((tmp < 0) || (!_superblocktype_2_3 && !tmp))
								_toneLevel[ch][sb][i] = 0;
							else
								_toneLevel[ch][sb][i] = fft_tone_level_table[tab][tmp & 0x3f];
						}
					}
				} else {
					for (ch = 0; ch < _channels; ch++) {
						for (i = 0; i < 64; i++) {
							tmp = _toneLevelIdx[ch][sb][i] = _toneLevelIdxBase[ch][sb][i / 8];
							if ((tmp < 0) || (!_superblocktype_2_3 && !tmp))
								_toneLevel[ch][sb][i] = 0;
							else
								_toneLevel[ch][sb][i] = fft_tone_level_table[tab][tmp & 0x3f];
						}
					}
				}
			}
		}
	}
}

/**
 * Related to synthesis filter
 * Called by process_subpacket_11
 * c is built with data from subpacket 11
 * Most of this function is used only if superblock_type_2_3 == 0, never seen it in samples
 *
 * @param tone_level_idx
 * @param tone_level_idx_temp
 * @param coding_method        q->coding_method[0][0][0]
 * @param nb_channels          number of channels
 * @param c                    coming from subpacket 11, passed as 8*c
 * @param superblocktype_2_3   flag based on superblock packet type
 * @param cm_table_select      q->cm_table_select
 */
void QDM2Stream::fill_coding_method_array(sb_int8_array tone_level_idx, sb_int8_array tone_level_idx_temp,
				sb_int8_array coding_method, int nb_channels,
				int c, int superblocktype_2_3, int cm_table_select) {
	int ch, sb, j;
	int tmp, acc, esp_40, comp;
	int add1, add2, add3, add4;
	int64 multres;

	// This should never happen
	if (nb_channels <= 0)
		return;
	if (!superblocktype_2_3) {
		warning("QDM2 This case is untested, no samples available");
		for (ch = 0; ch < nb_channels; ch++) {
			for (sb = 0; sb < 30; sb++) {
				for (j = 1; j < 63; j++) {  // The loop only iterates to 63 so the code doesn't overflow the buffer
					add1 = tone_level_idx[ch][sb][j] - 10;
					if (add1 < 0)
						add1 = 0;
					add2 = add3 = add4 = 0;
					if (sb > 1) {
						add2 = tone_level_idx[ch][sb - 2][j] + tone_level_idx_offset_table[sb][0] - 6;
						if (add2 < 0)
							add2 = 0;
					}
					if (sb > 0) {
						add3 = tone_level_idx[ch][sb - 1][j] + tone_level_idx_offset_table[sb][1] - 6;
						if (add3 < 0)
							add3 = 0;
					}
					if (sb < 29) {
						add4 = tone_level_idx[ch][sb + 1][j] + tone_level_idx_offset_table[sb][3] - 6;
						if (add4 < 0)
							add4 = 0;
					}
					tmp = tone_level_idx[ch][sb][j + 1] * 2 - add4 - add3 - add2 - add1;
					if (tmp < 0)
						tmp = 0;
					tone_level_idx_temp[ch][sb][j + 1] = tmp & 0xff;
				}
				tone_level_idx_temp[ch][sb][0] = tone_level_idx_temp[ch][sb][1];
			}
		}
		acc = 0;
		for (ch = 0; ch < nb_channels; ch++)
			for (sb = 0; sb < 30; sb++)
				for (j = 0; j < 64; j++)
					acc += tone_level_idx_temp[ch][sb][j];

		multres = 0x66666667 * (acc * 10);
		esp_40 = (multres >> 32) / 8 + ((multres & 0xffffffff) >> 31);
		for (ch = 0;  ch < nb_channels; ch++) {
			for (sb = 0; sb < 30; sb++) {
				for (j = 0; j < 64; j++) {
					comp = tone_level_idx_temp[ch][sb][j]* esp_40 * 10;
					if (comp < 0)
						comp += 0xff;
					comp /= 256; // signed shift
					switch(sb) {
						case 0:
							if (comp < 30)
								comp = 30;
							comp += 15;
							break;
						case 1:
							if (comp < 24)
								comp = 24;
							comp += 10;
							break;
						case 2:
						case 3:
						case 4:
							if (comp < 16)
								comp = 16;
							break;
						default:
							break;
					}
					if (comp <= 5)
						tmp = 0;
					else if (comp <= 10)
						tmp = 10;
					else if (comp <= 16)
						tmp = 16;
					else if (comp <= 24)
						tmp = -1;
					else
						tmp = 0;
					coding_method[ch][sb][j] = ((tmp & 0xfffa) + 30 )& 0xff;
				}
			}
		}
		for (sb = 0; sb < 30; sb++)
			fix_coding_method_array(sb, nb_channels, coding_method);
		for (ch = 0; ch < nb_channels; ch++) {
			for (sb = 0; sb < 30; sb++) {
				for (j = 0; j < 64; j++) {
					if (sb >= 10) {
						if (coding_method[ch][sb][j] < 10)
							coding_method[ch][sb][j] = 10;
					} else {
						if (sb >= 2) {
							if (coding_method[ch][sb][j] < 16)
								coding_method[ch][sb][j] = 16;
						} else {
							if (coding_method[ch][sb][j] < 30)
								coding_method[ch][sb][j] = 30;
						}
					}
				}
			}
		}
	} else { // superblocktype_2_3 != 0
		for (ch = 0; ch < nb_channels; ch++)
			for (sb = 0; sb < 30; sb++)
				for (j = 0; j < 64; j++)
					coding_method[ch][sb][j] = coding_method_table[cm_table_select][sb];
	}
}

/**
 *
 * Called by process_subpacket_11 to process more data from subpacket 11 with sb 0-8
 * Called by process_subpacket_12 to process data from subpacket 12 with sb 8-sb_used
 *
 * @param gb        bitreader context
 * @param length    packet length in bits
 * @param sb_min    lower subband processed (sb_min included)
 * @param sb_max    higher subband processed (sb_max excluded)
 */
void QDM2Stream::synthfilt_build_sb_samples(Common::BitStreamMemory32LELSB *gb, int length, int sb_min, int sb_max) {
	int sb, j, k, n, ch, run, channels;
	int joined_stereo, zero_encoding, chs;
	int type34_first;
	float type34_div = 0;
	float type34_predictor;
	float samples[10], sign_bits[16];

	if (length == 0) {
		// If no data use noise
		for (sb = sb_min; sb < sb_max; sb++)
			build_sb_samples_from_noise(sb);

		return;
	}

	for (sb = sb_min; sb < sb_max; sb++) {
		FIX_NOISE_IDX(_noiseIdx);

		channels = _channels;

		if (_channels <= 1 || sb < 12)
			joined_stereo = 0;
		else if (sb >= 24)
			joined_stereo = 1;
		else
			joined_stereo = ((length - gb->pos()) >= 1) ? gb->getBit() : 0;

		if (joined_stereo) {
			if ((length - gb->pos()) >= 16)
				for (j = 0; j < 16; j++)
					sign_bits[j] = gb->getBit();

			for (j = 0; j < 64; j++)
				if (_codingMethod[1][sb][j] > _codingMethod[0][sb][j])
					_codingMethod[0][sb][j] = _codingMethod[1][sb][j];

			fix_coding_method_array(sb, _channels, _codingMethod);
			channels = 1;
		}

		for (ch = 0; ch < channels; ch++) {
			zero_encoding = ((length - gb->pos()) >= 1) ? gb->getBit() : 0;
			type34_predictor = 0.0;
			type34_first = 1;

			for (j = 0; j < 128; ) {
				switch (_codingMethod[ch][sb][j / 2]) {
					case 8:
						if ((length - gb->pos()) >= 10) {
							if (zero_encoding) {
								for (k = 0; k < 5; k++) {
									if ((j + 2 * k) >= 128)
										break;
									samples[2 * k] = gb->getBit() ? dequant_1bit[joined_stereo][2 * gb->getBit()] : 0;
								}
							} else {
								n = gb->getBits<8>();
								for (k = 0; k < 5; k++)
									samples[2 * k] = dequant_1bit[joined_stereo][_randomDequantIndex[n][k]];
							}
							for (k = 0; k < 5; k++)
								samples[2 * k + 1] = SB_DITHERING_NOISE(sb, _noiseIdx);
						} else {
							for (k = 0; k < 10; k++)
								samples[k] = SB_DITHERING_NOISE(sb, _noiseIdx);
						}
						run = 10;
						break;

					case 10:
						if ((length - gb->pos()) >= 1) {
							double f = 0.81;

							if (gb->getBit())
								f = -f;
							f -= _noiseSamples[((sb + 1) * (j +5 * ch + 1)) & 127] * 9.0 / 40.0;
							samples[0] = f;
						} else {
							samples[0] = SB_DITHERING_NOISE(sb, _noiseIdx);
						}
						run = 1;
						break;

					case 16:
						if ((length - gb->pos()) >= 10) {
							if (zero_encoding) {
								for (k = 0; k < 5; k++) {
									if ((j + k) >= 128)
										break;
									samples[k] = (gb->getBit() == 0) ? 0 : dequant_1bit[joined_stereo][2 * gb->getBit()];
								}
							} else {
								n = gb->getBits<8>();
								for (k = 0; k < 5; k++)
									samples[k] = dequant_1bit[joined_stereo][_randomDequantIndex[n][k]];
							}
						} else {
							for (k = 0; k < 5; k++)
								samples[k] = SB_DITHERING_NOISE(sb, _noiseIdx);
						}
						run = 5;
						break;

					case 24:
						if ((length - gb->pos()) >= 7) {
							n = gb->getBits<7>();
							for (k = 0; k < 3; k++)
								samples[k] = (_randomDequantType24[n][k] - 2.0) * 0.5;
						} else {
							for (k = 0; k < 3; k++)
								samples[k] = SB_DITHERING_NOISE(sb, _noiseIdx);
						}
						run = 3;
						break;

					case 30:
						if ((length - gb->pos()) >= 4)
							samples[0] = type30_dequant[qdm2_get_vlc(gb, &_vlcTabType30, 0, 1)];
						else
							samples[0] = SB_DITHERING_NOISE(sb, _noiseIdx);

						run = 1;
						break;

					case 34:
						if ((length - gb->pos()) >= 7) {
							if (type34_first) {
								type34_div = (float)(1 << gb->getBits<2>());
								samples[0] = ((float)gb->getBits<5>() - 16.0) / 15.0;
								type34_predictor = samples[0];
								type34_first = 0;
							} else {
								samples[0] = type34_delta[qdm2_get_vlc(gb, &_vlcTabType34, 0, 1)] / type34_div + type34_predictor;
								type34_predictor = samples[0];
							}
						} else {
							samples[0] = SB_DITHERING_NOISE(sb, _noiseIdx);
						}
						run = 1;
						break;

					default:
						samples[0] = SB_DITHERING_NOISE(sb, _noiseIdx);
						run = 1;
						break;
				}

				if (joined_stereo) {
					float tmp[10][MPA_MAX_CHANNELS];

					for (k = 0; k < run; k++) {
						tmp[k][0] = samples[k];
						tmp[k][1] = (sign_bits[(j + k) / 8]) ? -samples[k] : samples[k];
					}
					for (chs = 0; chs < _channels; chs++)
						for (k = 0; k < run; k++)
							if ((j + k) < 128)
								_sbSamples[chs][j + k][sb] = (int32)(_toneLevel[chs][sb][((j + k)/2)] * tmp[k][chs] + .5);
				} else {
					for (k = 0; k < run; k++)
						if ((j + k) < 128)
							_sbSamples[ch][j + k][sb] = (int32)(_toneLevel[ch][sb][(j + k)/2] * samples[k] + .5);
				}

				j += run;
			} // j loop
		} // channel loop
	} // subband loop
}

/**
 * Init the first element of a channel in quantized_coeffs with data from packet 10 (quantized_coeffs[ch][0]).
 * This is similar to process_subpacket_9, but for a single channel and for element [0]
 * same VLC tables as process_subpacket_9 are used.
 *
 * @param quantized_coeffs    pointer to quantized_coeffs[ch][0]
 * @param gb        bitreader context
 * @param length    packet length in bits
 */
void QDM2Stream::init_quantized_coeffs_elem0(int8 *quantized_coeffs, Common::BitStreamMemory32LELSB *gb, int length) {
	int i, k, run, level, diff;

	if ((length - gb->pos()) < 16)
		return;
	level = qdm2_get_vlc(gb, &_vlcTabLevel, 0, 2);

	quantized_coeffs[0] = level;

	for (i = 0; i < 7; ) {
		if ((length - gb->pos()) < 16)
			break;
		run = qdm2_get_vlc(gb, &_vlcTabRun, 0, 1) + 1;

		if ((length - gb->pos()) < 16)
			break;
		diff = qdm2_get_se_vlc(&_vlcTabDiff, gb, 2);

		for (k = 1; k <= run; k++)
			quantized_coeffs[i + k] = (level + ((k * diff) / run));

		level += diff;
		i += run;
	}
}

/**
 * Related to synthesis filter, process data from packet 10
 * Init part of quantized_coeffs via function init_quantized_coeffs_elem0
 * Init tone_level_idx_hi1, tone_level_idx_hi2, tone_level_idx_mid with data from packet 10
 *
 * @param gb        bitreader context
 * @param length    packet length in bits
 */
void QDM2Stream::init_tone_level_dequantization(Common::BitStreamMemory32LELSB *gb, int length) {
	int sb, j, k, n, ch;

	for (ch = 0; ch < _channels; ch++) {
		init_quantized_coeffs_elem0(_quantizedCoeffs[ch][0], gb, length);

		if ((length - gb->pos()) < 16) {
			memset(_quantizedCoeffs[ch][0], 0, 8);
			break;
		}
	}

	n = _subSampling + 1;

	for (sb = 0; sb < n; sb++)
		for (ch = 0; ch < _channels; ch++)
			for (j = 0; j < 8; j++) {
				if ((length - gb->pos()) < 1)
					break;
				if (gb->getBit()) {
					for (k=0; k < 8; k++) {
						if ((length - gb->pos()) < 16)
							break;
						_toneLevelIdxHi1[ch][sb][j][k] = qdm2_get_vlc(gb, &_vlcTabToneLevelIdxHi1, 0, 2);
					}
				} else {
					for (k=0; k < 8; k++)
						_toneLevelIdxHi1[ch][sb][j][k] = 0;
				}
			}

	n = QDM2_SB_USED(_subSampling) - 4;

	for (sb = 0; sb < n; sb++)
		for (ch = 0; ch < _channels; ch++) {
			if ((length - gb->pos()) < 16)
				break;
			_toneLevelIdxHi2[ch][sb] = qdm2_get_vlc(gb, &_vlcTabToneLevelIdxHi2, 0, 2);
			if (sb > 19)
				_toneLevelIdxHi2[ch][sb] -= 16;
			else
				for (j = 0; j < 8; j++)
					_toneLevelIdxMid[ch][sb][j] = -16;
		}

	n = QDM2_SB_USED(_subSampling) - 5;

	for (sb = 0; sb < n; sb++) {
		for (ch = 0; ch < _channels; ch++) {
			for (j = 0; j < 8; j++) {
				if ((length - gb->pos()) < 16)
					break;
				_toneLevelIdxMid[ch][sb][j] = qdm2_get_vlc(gb, &_vlcTabToneLevelIdxMid, 0, 2) - 32;
			}
		}
	}
}

/**
 * Process subpacket 9, init quantized_coeffs with data from it
 *
 * @param node    pointer to node with packet
 */
void QDM2Stream::process_subpacket_9(QDM2SubPNode *node) {
	int i, j, k, n, ch, run, level, diff;

	Common::BitStreamMemoryStream d(node->packet->data, node->packet->size + FF_INPUT_BUFFER_PADDING_SIZE);
	Common::BitStreamMemory32LELSB gb(&d);

	n = coeff_per_sb_for_avg[_coeffPerSbSelect][QDM2_SB_USED(_subSampling) - 1] + 1; // same as averagesomething function

	for (i = 1; i < n; i++)
		for (ch = 0; ch < _channels; ch++) {
			level = qdm2_get_vlc(&gb, &_vlcTabLevel, 0, 2);
			_quantizedCoeffs[ch][i][0] = level;

			for (j = 0; j < (8 - 1); ) {
				run = qdm2_get_vlc(&gb, &_vlcTabRun, 0, 1) + 1;
				diff = qdm2_get_se_vlc(&_vlcTabDiff, &gb, 2);

				for (k = 1; k <= run; k++)
					_quantizedCoeffs[ch][i][j + k] = (level + ((k*diff) / run));

				level += diff;
				j += run;
			}
		}

	for (ch = 0; ch < _channels; ch++)
		for (i = 0; i < 8; i++)
			_quantizedCoeffs[ch][0][i] = 0;
}

/**
 * Process subpacket 10 if not null, else
 *
 * @param node      pointer to node with packet
 * @param length    packet length in bits
 */
void QDM2Stream::process_subpacket_10(QDM2SubPNode *node, int length) {
	Common::BitStreamMemoryStream d(((node == NULL) ? _emptyBuffer : node->packet->data), ((node == NULL) ? 0 : node->packet->size + FF_INPUT_BUFFER_PADDING_SIZE));
	Common::BitStreamMemory32LELSB gb(&d);

	if (length != 0) {
		init_tone_level_dequantization(&gb, length);
		fill_tone_level_array(1);
	} else {
		fill_tone_level_array(0);
	}
}

/**
 * Process subpacket 11
 *
 * @param node      pointer to node with packet
 * @param length    packet length in bit
 */
void QDM2Stream::process_subpacket_11(QDM2SubPNode *node, int length) {
	Common::BitStreamMemoryStream d(((node == NULL) ? _emptyBuffer : node->packet->data), ((node == NULL) ? 0 : node->packet->size + FF_INPUT_BUFFER_PADDING_SIZE));
	Common::BitStreamMemory32LELSB gb(&d);

	if (length >= 32) {
		int c = gb.getBits<13>();

		if (c > 3)
			fill_coding_method_array(_toneLevelIdx, _toneLevelIdxTemp, _codingMethod,
			                         _channels, 8*c, _superblocktype_2_3, _cmTableSelect);
	}

	synthfilt_build_sb_samples(&gb, length, 0, 8);
}

/**
 * Process subpacket 12
 *
 * @param node      pointer to node with packet
 * @param length    packet length in bits
 */
void QDM2Stream::process_subpacket_12(QDM2SubPNode *node, int length) {
	Common::BitStreamMemoryStream d(((node == NULL) ? _emptyBuffer : node->packet->data), ((node == NULL) ? 0 : node->packet->size + FF_INPUT_BUFFER_PADDING_SIZE));
	Common::BitStreamMemory32LELSB gb(&d);

	synthfilt_build_sb_samples(&gb, length, 8, QDM2_SB_USED(_subSampling));
}

/*
 * Process new subpackets for synthesis filter
 *
 * @param list    list with synthesis filter packets (list D)
 */
void QDM2Stream::process_synthesis_subpackets(QDM2SubPNode *list) {
	struct QDM2SubPNode *nodes[4];

	nodes[0] = qdm2_search_subpacket_type_in_list(list, 9);
	if (nodes[0] != NULL)
		process_subpacket_9(nodes[0]);

	nodes[1] = qdm2_search_subpacket_type_in_list(list, 10);
	if (nodes[1] != NULL)
		process_subpacket_10(nodes[1], nodes[1]->packet->size << 3);
	else
		process_subpacket_10(NULL, 0);

	nodes[2] = qdm2_search_subpacket_type_in_list(list, 11);
	if (nodes[0] != NULL && nodes[1] != NULL && nodes[2] != NULL)
		process_subpacket_11(nodes[2], (nodes[2]->packet->size << 3));
	else
		process_subpacket_11(NULL, 0);

	nodes[3] = qdm2_search_subpacket_type_in_list(list, 12);
	if (nodes[0] != NULL && nodes[1] != NULL && nodes[3] != NULL)
		process_subpacket_12(nodes[3], (nodes[3]->packet->size << 3));
	else
		process_subpacket_12(NULL, 0);
}

/*
 * Decode superblock, fill packet lists.
 *
 */
void QDM2Stream::qdm2_decode_super_block(void) {
	struct QDM2SubPacket header, *packet;
	int i, packet_bytes, sub_packet_size, subPacketsD;
	unsigned int next_index = 0;

	memset(_toneLevelIdxHi1, 0, sizeof(_toneLevelIdxHi1));
	memset(_toneLevelIdxMid, 0, sizeof(_toneLevelIdxMid));
	memset(_toneLevelIdxHi2, 0, sizeof(_toneLevelIdxHi2));

	_subPacketsB = 0;
	subPacketsD = 0;

	average_quantized_coeffs(); // average elements in quantized_coeffs[max_ch][10][8]

	Common::BitStreamMemoryStream packetStream(_compressedData, _packetSize + FF_INPUT_BUFFER_PADDING_SIZE);
	Common::BitStreamMemory32LELSB packetBitStream(packetStream);
	//qdm2_decode_sub_packet_header
	header.type = packetBitStream.getBits<8>();

	if (header.type == 0) {
		header.size = 0;
		header.data = NULL;
	} else {
		header.size = packetBitStream.getBits<8>();

		if (header.type & 0x80) {
			header.size <<= 8;
			header.size |= packetBitStream.getBits<8>();
			header.type &= 0x7f;
		}

		if (header.type == 0x7f)
			header.type |= (packetBitStream.getBits<8>() << 8);

		header.data = &_compressedData[packetBitStream.pos() / 8];
	}

	if (header.type < 2 || header.type >= 8) {
		_hasErrors = true;
		error("QDM2 : bad superblock type");
		return;
	}

	_superblocktype_2_3 = (header.type == 2 || header.type == 3);
	packet_bytes = (_packetSize - packetBitStream.pos() / 8);

	Common::BitStreamMemoryStream headerStream(header.data, header.size + FF_INPUT_BUFFER_PADDING_SIZE);
	Common::BitStreamMemory32LELSB headerBitStream(headerStream);

	if (header.type == 2 || header.type == 4 || header.type == 5) {
		int csum = 257 * headerBitStream.getBits<8>() + 2 * headerBitStream.getBits<8>();

		csum = qdm2_packet_checksum(_compressedData, _packetSize, csum);

		if (csum != 0) {
			_hasErrors = true;
			error("QDM2 : bad packet checksum");
			return;
		}
	}

	_subPacketListB[0].packet = NULL;
	_subPacketListD[0].packet = NULL;

	for (i = 0; i < 6; i++)
		if (--_fftLevelExp[i] < 0)
			_fftLevelExp[i] = 0;

	for (i = 0; packet_bytes > 0; i++) {
		int j;

		_subPacketListA[i].next = NULL;

		if (i > 0) {
			_subPacketListA[i - 1].next = &_subPacketListA[i];

			if (next_index >= header.size)
				break;

			// seek to next block
			headerBitStream.skip(next_index * 8 - headerBitStream.pos());
		}

		// decode subpacket
		packet = &_subPackets[i];
		//qdm2_decode_sub_packet_header
		packet->type = headerBitStream.getBits<8>();

		if (packet->type == 0) {
			packet->size = 0;
			packet->data = NULL;
		} else {
			packet->size = headerBitStream.getBits<8>();

			if (packet->type & 0x80) {
				packet->size <<= 8;
				packet->size |= headerBitStream.getBits<8>();
				packet->type &= 0x7f;
			}

			if (packet->type == 0x7f)
				packet->type |= (headerBitStream.getBits<8>() << 8);

			packet->data = &header.data[headerBitStream.pos() / 8];
		}

		next_index = packet->size + headerBitStream.pos() / 8;
		sub_packet_size = ((packet->size > 0xff) ? 1 : 0) + packet->size + 2;

		if (packet->type == 0)
			break;

		if (sub_packet_size > packet_bytes) {
			if (packet->type != 10 && packet->type != 11 && packet->type != 12)
				break;
			packet->size += packet_bytes - sub_packet_size;
		}

		packet_bytes -= sub_packet_size;

		// add subpacket to 'all subpackets' list
		_subPacketListA[i].packet = packet;

		// add subpacket to related list
		if (packet->type == 8) {
			error("Unsupported packet type 8");
			return;
		} else if (packet->type >= 9 && packet->type <= 12) {
			// packets for MPEG Audio like Synthesis Filter
			QDM2_LIST_ADD(_subPacketListD, subPacketsD, packet);
		} else if (packet->type == 13) {
			for (j = 0; j < 6; j++)
				_fftLevelExp[j] = headerBitStream.getBits<6>();
		} else if (packet->type == 14) {
			for (j = 0; j < 6; j++)
				_fftLevelExp[j] = qdm2_get_vlc(&headerBitStream, &_fftLevelExpVlc, 0, 2);
		} else if (packet->type == 15) {
			error("Unsupported packet type 15");
			return;
		} else if (packet->type >= 16 && packet->type < 48 && !fft_subpackets[packet->type - 16]) {
			// packets for FFT
			QDM2_LIST_ADD(_subPacketListB, _subPacketsB, packet);
		}
	} // Packet bytes loop

// ****************************************************************
	if (_subPacketListD[0].packet != NULL) {
		process_synthesis_subpackets(_subPacketListD);
		_doSynthFilter = 1;
	} else if (_doSynthFilter) {
		process_subpacket_10(NULL, 0);
		process_subpacket_11(NULL, 0);
		process_subpacket_12(NULL, 0);
	}
// ****************************************************************
}

void QDM2Stream::qdm2_fft_init_coefficient(int sub_packet, int offset, int duration,
										   int channel, int exp, int phase) {
	if (_fftCoefsMinIndex[duration] < 0)
	    _fftCoefsMinIndex[duration] = _fftCoefsIndex;

	_fftCoefs[_fftCoefsIndex].sub_packet = ((sub_packet >= 16) ? (sub_packet - 16) : sub_packet);
	_fftCoefs[_fftCoefsIndex].channel = channel;
	_fftCoefs[_fftCoefsIndex].offset = offset;
	_fftCoefs[_fftCoefsIndex].exp = exp;
	_fftCoefs[_fftCoefsIndex].phase = phase;
	_fftCoefsIndex++;
}

void QDM2Stream::qdm2_fft_decode_tones(int duration, Common::BitStreamMemory32LELSB *gb, int b) {
	int channel, stereo, phase, exp;
	int local_int_4,  local_int_8,  stereo_phase,  local_int_10;
	int local_int_14, stereo_exp, local_int_20, local_int_28;
	int n, offset;

	local_int_4 = 0;
	local_int_28 = 0;
	local_int_20 = 2;
	local_int_8 = (4 - duration);
	local_int_10 = 1 << (_groupOrder - duration - 1);
	offset = 1;

	while (1) {
		if (_superblocktype_2_3) {
			while ((n = qdm2_get_vlc(gb, &_vlcTabFftToneOffset[local_int_8], 1, 2)) < 2) {
				offset = 1;
				if (n == 0) {
					local_int_4 += local_int_10;
					local_int_28 += (1 << local_int_8);
				} else {
					local_int_4 += 8*local_int_10;
					local_int_28 += (8 << local_int_8);
				}
			}
			offset += (n - 2);
		} else {
			offset += qdm2_get_vlc(gb, &_vlcTabFftToneOffset[local_int_8], 1, 2);
			while (offset >= (local_int_10 - 1)) {
				offset += (1 - (local_int_10 - 1));
				local_int_4  += local_int_10;
				local_int_28 += (1 << local_int_8);
			}
		}

		if (local_int_4 >= _blockSize)
			return;

		local_int_14 = (offset >> local_int_8);

		if (_channels > 1) {
			channel = gb->getBit();
			stereo = gb->getBit();
		} else {
			channel = 0;
			stereo = 0;
		}

		exp = qdm2_get_vlc(gb, (b ? &_fftLevelExpVlc : &_fftLevelExpAltVlc), 0, 2);
		exp += _fftLevelExp[fft_level_index_table[local_int_14]];
		exp = (exp < 0) ? 0 : exp;

		phase = gb->getBits<3>();
		stereo_exp = 0;
		stereo_phase = 0;

		if (stereo) {
			stereo_exp = (exp - qdm2_get_vlc(gb, &_fftStereoExpVlc, 0, 1));
			stereo_phase = (phase - qdm2_get_vlc(gb, &_fftStereoPhaseVlc, 0, 1));
			if (stereo_phase < 0)
				stereo_phase += 8;
		}

		if (_frequencyRange > (local_int_14 + 1)) {
			int sub_packet = (local_int_20 + local_int_28);

			qdm2_fft_init_coefficient(sub_packet, offset, duration, channel, exp, phase);
			if (stereo)
				qdm2_fft_init_coefficient(sub_packet, offset, duration, (1 - channel), stereo_exp, stereo_phase);
		}

		offset++;
	}
}

void QDM2Stream::qdm2_decode_fft_packets(void) {
	int i, j, min, max, value, type, unknown_flag;

	if (_subPacketListB[0].packet == NULL)
		return;

	// reset minimum indexes for FFT coefficients
	_fftCoefsIndex = 0;
	for (i=0; i < 5; i++)
		_fftCoefsMinIndex[i] = -1;

	// process subpackets ordered by type, largest type first
	for (i = 0, max = 256; i < _subPacketsB; i++) {
		QDM2SubPacket *packet= NULL;

		// find subpacket with largest type less than max
		for (j = 0, min = 0; j < _subPacketsB; j++) {
			value = _subPacketListB[j].packet->type;
			if (value > min && value < max) {
				min = value;
				packet = _subPacketListB[j].packet;
			}
		}

		max = min;

		// check for errors (?)
		if (!packet)
			return;

		if (i == 0 && (packet->type < 16 || packet->type >= 48 || fft_subpackets[packet->type - 16]))
			return;

		// decode FFT tones
		Common::BitStreamMemoryStream d(packet->data, packet->size + FF_INPUT_BUFFER_PADDING_SIZE);
		Common::BitStreamMemory32LELSB gb(&d);

		if (packet->type >= 32 && packet->type < 48 && !fft_subpackets[packet->type - 16])
			unknown_flag = 1;
		else
			unknown_flag = 0;

		type = packet->type;

		if ((type >= 17 && type < 24) || (type >= 33 && type < 40)) {
			int duration = _subSampling + 5 - (type & 15);

			if (duration >= 0 && duration < 4) { // TODO: Should be <= 4?
				qdm2_fft_decode_tones(duration, &gb, unknown_flag);
			}
		} else if (type == 31) {
			for (j=0; j < 4; j++) {
				qdm2_fft_decode_tones(j, &gb, unknown_flag);
			}
		} else if (type == 46) {
			for (j=0; j < 6; j++)
				_fftLevelExp[j] = gb.getBits<6>();
			for (j=0; j < 4; j++) {
				qdm2_fft_decode_tones(j, &gb, unknown_flag);
			}
		}
	} // Loop on B packets

	// calculate maximum indexes for FFT coefficients
	for (i = 0, j = -1; i < 5; i++)
		if (_fftCoefsMinIndex[i] >= 0) {
			if (j >= 0)
				_fftCoefsMaxIndex[j] = _fftCoefsMinIndex[i];
			j = i;
		}
	if (j >= 0)
		_fftCoefsMaxIndex[j] = _fftCoefsIndex;
}

void QDM2Stream::qdm2_fft_generate_tone(FFTTone *tone)
{
	float level, f[6];
	int i;
	QDM2Complex c;
	const double iscale = 2.0 * M_PI / 512.0;

	tone->phase += tone->phase_shift;

	// calculate current level (maximum amplitude) of tone
	level = fft_tone_envelope_table[tone->duration][tone->time_index] * tone->level;
	c.im = level * sin(tone->phase*iscale);
	c.re = level * cos(tone->phase*iscale);

	// generate FFT coefficients for tone
	if (tone->duration >= 3 || tone->cutoff >= 3) {
	    tone->complex[0].im += c.im;
	    tone->complex[0].re += c.re;
	    tone->complex[1].im -= c.im;
	    tone->complex[1].re -= c.re;
	} else {
		f[1] = -tone->table[4];
		f[0] =  tone->table[3] - tone->table[0];
		f[2] =  1.0 - tone->table[2] - tone->table[3];
		f[3] =  tone->table[1] + tone->table[4] - 1.0;
		f[4] =  tone->table[0] - tone->table[1];
		f[5] =  tone->table[2];
		for (i = 0; i < 2; i++) {
			tone->complex[fft_cutoff_index_table[tone->cutoff][i]].re += c.re * f[i];
			tone->complex[fft_cutoff_index_table[tone->cutoff][i]].im += c.im *((tone->cutoff <= i) ? -f[i] : f[i]);
		}
		for (i = 0; i < 4; i++) {
			tone->complex[i].re += c.re * f[i+2];
			tone->complex[i].im += c.im * f[i+2];
		}
	}

	// copy the tone if it has not yet died out
	if (++tone->time_index < ((1 << (5 - tone->duration)) - 1)) {
		memcpy(&_fftTones[_fftToneEnd], tone, sizeof(FFTTone));
		_fftToneEnd = (_fftToneEnd + 1) % 1000;
	}
}

void QDM2Stream::qdm2_fft_tone_synthesizer(uint8 sub_packet) {
	int i, j, ch;
	const double iscale = 0.25 * M_PI;

	for (ch = 0; ch < _channels; ch++) {
		memset(_fft.complex[ch], 0, _frameSize * sizeof(QDM2Complex));
	}

	// apply FFT tones with duration 4 (1 FFT period)
	if (_fftCoefsMinIndex[4] >= 0)
		for (i = _fftCoefsMinIndex[4]; i < _fftCoefsMaxIndex[4]; i++) {
			float level;
			QDM2Complex c;

			if (_fftCoefs[i].sub_packet != sub_packet)
				break;

			ch = (_channels == 1) ? 0 : _fftCoefs[i].channel;
			level = (_fftCoefs[i].exp < 0) ? 0.0 : fft_tone_level_table[_superblocktype_2_3 ? 0 : 1][_fftCoefs[i].exp & 63];

			c.re = level * cos(_fftCoefs[i].phase * iscale);
			c.im = level * sin(_fftCoefs[i].phase * iscale);
			_fft.complex[ch][_fftCoefs[i].offset + 0].re += c.re;
			_fft.complex[ch][_fftCoefs[i].offset + 0].im += c.im;
			_fft.complex[ch][_fftCoefs[i].offset + 1].re -= c.re;
			_fft.complex[ch][_fftCoefs[i].offset + 1].im -= c.im;
		}

	// generate existing FFT tones
	for (i = _fftToneEnd; i != _fftToneStart; ) {
		qdm2_fft_generate_tone(&_fftTones[_fftToneStart]);
		_fftToneStart = (_fftToneStart + 1) % 1000;
	}

	// create and generate new FFT tones with duration 0 (long) to 3 (short)
	for (i = 0; i < 4; i++)
		if (_fftCoefsMinIndex[i] >= 0) {
			for (j = _fftCoefsMinIndex[i]; j < _fftCoefsMaxIndex[i]; j++) {
				int offset, four_i;
				FFTTone tone;

				if (_fftCoefs[j].sub_packet != sub_packet)
					break;

				four_i = (4 - i);
				offset = _fftCoefs[j].offset >> four_i;
				ch = (_channels == 1) ? 0 : _fftCoefs[j].channel;

				if (offset < _frequencyRange) {
					if (offset < 2)
						tone.cutoff = offset;
					else
						tone.cutoff = (offset >= 60) ? 3 : 2;

					tone.level = (_fftCoefs[j].exp < 0) ? 0.0 : fft_tone_level_table[_superblocktype_2_3 ? 0 : 1][_fftCoefs[j].exp & 63];
					tone.complex = &_fft.complex[ch][offset];
					tone.table = fft_tone_sample_table[i][_fftCoefs[j].offset - (offset << four_i)];
					tone.phase = 64 * _fftCoefs[j].phase - (offset << 8) - 128;
					tone.phase_shift = (2 * _fftCoefs[j].offset + 1) << (7 - four_i);
					tone.duration = i;
					tone.time_index = 0;

					qdm2_fft_generate_tone(&tone);
				}
			}
			_fftCoefsMinIndex[i] = j;
		}
}

void QDM2Stream::qdm2_calculate_fft(int channel) {
	_fft.complex[channel][0].re *= 2.0f;
	_fft.complex[channel][0].im = 0.0f;

	_rdft->calc((float *)_fft.complex[channel]);

	// add samples to output buffer
	for (int i = 0; i < ((_fftFrameSize + 15) & ~15); i++)
		_outputBuffer[_channels * i + channel] += ((float *) _fft.complex[channel])[i];
}

/**
 * @param index    subpacket number
 */
void QDM2Stream::qdm2_synthesis_filter(uint8 index)
{
	int16 samples[MPA_MAX_CHANNELS * MPA_FRAME_SIZE];
	int i, k, ch, sb_used, sub_sampling, dither_state = 0;

	// copy sb_samples
	sb_used = QDM2_SB_USED(_subSampling);

	for (ch = 0; ch < _channels; ch++)
		for (i = 0; i < 8; i++)
			for (k = sb_used; k < 32; k++)
				_sbSamples[ch][(8 * index) + i][k] = 0;

	for (ch = 0; ch < _channels; ch++) {
		int16 *samples_ptr = samples + ch;

		for (i = 0; i < 8; i++) {
			ff_mpa_synth_filter(_synthBuf[ch], &(_synthBufOffset[ch]),
			                    ff_mpa_synth_window, &dither_state,
			                    samples_ptr, _channels,
			                    _sbSamples[ch][(8 * index) + i]);
			samples_ptr += 32 * _channels;
		}
	}

	// add samples to output buffer
	sub_sampling = (4 >> _subSampling);

	for (ch = 0; ch < _channels; ch++)
		for (i = 0; i < _sFrameSize; i++)
			_outputBuffer[_channels * i + ch] += (float)(samples[_channels * sub_sampling * i + ch] >> (sizeof(int16)*8-16));
}

bool QDM2Stream::qdm2_decodeFrame(Common::SeekableReadStream &in, QueuingAudioStream *audioStream) {
	debug(1, "QDM2Stream::qdm2_decodeFrame in.pos(): %ld in.size(): %ld", long(in.pos()), long(in.size()));
	int ch, i;
	const int frame_size = (_sFrameSize * _channels);

	// If we're in any packet but the first, seek back to the first
	if (_subPacket == 0)
		_superBlockStart = in.pos();
	else
		in.seek(_superBlockStart);

	// select input buffer
	if (in.eos() || in.pos() >= in.size()) {
		debug(1, "QDM2Stream::qdm2_decodeFrame End of Input Stream");
		return false;
	}

	if ((in.size() - in.pos()) < _packetSize) {
		debug(1, "QDM2Stream::qdm2_decodeFrame Insufficient Packet Data in Input Stream Found: %ld Need: %d", long(in.size() - in.pos()), _packetSize);
		return false;
	}

	if (!in.eos()) {
		in.read(_compressedData, _packetSize);
		memset(_compressedData + _packetSize, 0, FF_INPUT_BUFFER_PADDING_SIZE);
		debug(1, "QDM2Stream::qdm2_decodeFrame constructed input data");
	}

	// copy old block, clear new block of output samples
	memmove(_outputBuffer, &_outputBuffer[frame_size], frame_size * sizeof(float));
	memset(&_outputBuffer[frame_size], 0, frame_size * sizeof(float));
	debug(1, "QDM2Stream::qdm2_decodeFrame cleared outputBuffer");

	if (!in.eos()) {
		// decode block of QDM2 compressed data
		debug(1, "QDM2Stream::qdm2_decodeFrame decode block of QDM2 compressed data");
		if (_subPacket == 0) {
			_hasErrors = false; // reset it for a new super block
			debug(1, "QDM2 : Superblock follows");
			qdm2_decode_super_block();
		}

		// parse subpackets
		debug(1, "QDM2Stream::qdm2_decodeFrame parse subpackets");
		if (!_hasErrors) {
			if (_subPacket == 2) {
				debug(1, "QDM2Stream::qdm2_decodeFrame qdm2_decode_fft_packets()");
				qdm2_decode_fft_packets();
			}

			debug(1, "QDM2Stream::qdm2_decodeFrame qdm2_fft_tone_synthesizer(%d)", _subPacket);
			qdm2_fft_tone_synthesizer(_subPacket);
		}

		// sound synthesis stage 1 (FFT)
		debug(1, "QDM2Stream::qdm2_decodeFrame sound synthesis stage 1 (FFT)");
		for (ch = 0; ch < _channels; ch++) {
			qdm2_calculate_fft(ch);

			if (!_hasErrors && _subPacketListC[0].packet != NULL) {
				error("QDM2 : has errors, and C list is not empty");
				return false;
			}
		}

		// sound synthesis stage 2 (MPEG audio like synthesis filter)
		debug(1, "QDM2Stream::qdm2_decodeFrame sound synthesis stage 2 (MPEG audio like synthesis filter)");
		if (!_hasErrors && _doSynthFilter)
			qdm2_synthesis_filter(_subPacket);

		_subPacket = (_subPacket + 1) % 16;

		if(_hasErrors)
			warning("QDM2 Packet error...");

		// clip and convert output float[] to 16bit signed samples
		debug(1, "QDM2Stream::qdm2_decodeFrame clip and convert output float[] to 16bit signed samples");
	}

	if (frame_size == 0)
		return false;

	// Prepare a buffer for queuing
	uint16 *outputBuffer = (uint16 *)malloc(frame_size * 2);

	for (i = 0; i < frame_size; i++) {
		int value = (int)_outputBuffer[i];

		if (value > SOFTCLIP_THRESHOLD)
			value = (value >  HARDCLIP_THRESHOLD) ?  32767 :  _softclipTable[ value - SOFTCLIP_THRESHOLD];
		else if (value < -SOFTCLIP_THRESHOLD)
			value = (value < -HARDCLIP_THRESHOLD) ? -32767 : -_softclipTable[-value - SOFTCLIP_THRESHOLD];

		outputBuffer[i] = value;
	}

	// Queue the translated buffer to our stream
	byte flags = FLAG_16BITS;

	if (_channels == 2)
		flags |= FLAG_STEREO;

#ifdef SCUMM_LITTLE_ENDIAN
	flags |= FLAG_LITTLE_ENDIAN;
#endif

	audioStream->queueBuffer((byte *)outputBuffer, frame_size * 2, DisposeAfterUse::YES, flags);

	return true;
}

AudioStream *QDM2Stream::decodeFrame(Common::SeekableReadStream &stream) {
	QueuingAudioStream *audioStream = makeQueuingAudioStream(_sampleRate, _channels == 2);

	while (qdm2_decodeFrame(stream, audioStream))
		;

	audioStream->finish();
	return audioStream;
}

Codec *makeQDM2Decoder(Common::SeekableReadStream *extraData, DisposeAfterUse::Flag disposeExtraData) {
	return new QDM2Stream(extraData, disposeExtraData);
}

} // End of namespace Audio

