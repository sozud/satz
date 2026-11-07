#include "sega_mth.h"

static const MthMatrix unit_matrix = {{
    { MTH_FIXED(1), 0, 0, 0 },
    { 0, MTH_FIXED(1), 0, 0 },
    { 0, 0, MTH_FIXED(1), 0 },
}};

void MTH_InitialMatrix(MthMatrixTbl* matrixTbl, Uint16 stackSize, MthMatrix* matrix)
{
    matrixTbl->stackSize = stackSize;
    matrixTbl->stack = matrix;
    matrixTbl->current = matrix;
    MTH_ClearMatrix(matrixTbl);
}

void MTH_ClearMatrix(MthMatrixTbl* matrixTbl) { *matrixTbl->current = unit_matrix; }

static unsigned long long mul(Fixed32 a, Fixed32 b) { return (unsigned long long)((long long)a * b); }

static Fixed32 xtrct(unsigned long long acc) { return (Fixed32)(Uint32)(acc >> 16); }

static Fixed32 neg(Fixed32 a) { return (Fixed32)(0u - (Uint32)a); }

static Fixed32 add(Fixed32 a, Fixed32 b) { return (Fixed32)((Uint32)a + (Uint32)b); }

static Fixed32 dot3(const Fixed32* row, Fixed32 x, Fixed32 y, Fixed32 z)
{
    return xtrct(mul(row[0], x) + mul(row[1], y) + mul(row[2], z));
}

void MTH_PushMatrix(MthMatrixTbl* matrixTbl)
{
    MthMatrix* m = matrixTbl->current;
    m[1] = m[0];
    matrixTbl->current = m + 1;
}

void MTH_PopMatrix(MthMatrixTbl* matrixTbl) { matrixTbl->current--; }

void MTH_MoveMatrix(MthMatrixTbl* matrixTbl, Fixed32 x, Fixed32 y, Fixed32 z)
{
    Fixed32(*m)[4] = matrixTbl->current->val;
    for (int i = 0; i < 3; ++i)
        m[i][3] = add(dot3(m[i], x, y, z), m[i][3]);
}

static void rotate(MthMatrixTbl* matrixTbl, Fixed32 degree, int p, int q, Fixed32 (*sinFn)(Fixed32))
{
    Fixed32 s = sinFn(degree), c = MTH_Cos(degree);
    Fixed32(*m)[4] = matrixTbl->current->val;
    for (int i = 0; i < 3; ++i) {
        Fixed32 a = m[i][p], b = m[i][q];
        m[i][p] = xtrct(mul(a, c) + mul(b, s));
        m[i][q] = xtrct(mul(a, neg(s)) + mul(b, c));
    }
}

static Fixed32 negSin(Fixed32 degree) { return neg(MTH_Sin(degree)); }

void MTH_RotateMatrixX(MthMatrixTbl* matrixTbl, Fixed32 xDegree) { rotate(matrixTbl, xDegree, 1, 2, MTH_Sin); }

void MTH_RotateMatrixY(MthMatrixTbl* matrixTbl, Fixed32 yDegree) { rotate(matrixTbl, yDegree, 0, 2, negSin); }

void MTH_RotateMatrixZ(MthMatrixTbl* matrixTbl, Fixed32 zDegree) { rotate(matrixTbl, zDegree, 0, 1, MTH_Sin); }

void MTH_ReverseZ(MthMatrixTbl* matrixTbl)
{
    Fixed32(*m)[4] = matrixTbl->current->val;
    for (int i = 0; i < 3; ++i)
        m[i][2] = neg(m[i][2]);
}

void MTH_MulMatrix(MthMatrix* a, MthMatrix* b, MthMatrix* c)
{
    for (int j = 0; j < 4; ++j) {
        Fixed32 t0 = b->val[0][j], t1 = b->val[1][j], t2 = b->val[2][j];
        for (int i = 0; i < 3; ++i) {
            unsigned long long acc = mul(a->val[i][0], t0) + mul(a->val[i][1], t1) + mul(a->val[i][2], t2);
            if (j == 3)
                acc += mul(a->val[i][3], MTH_FIXED(1));
            c->val[i][j] = xtrct(acc);
        }
    }
}

void MTH_CoordTrans(MthMatrix* matrix, MthXyz* src, MthXyz* ans)
{
    Fixed32(*m)[4] = matrix->val;
    ans->x = add(dot3(m[0], src->x, src->y, src->z), m[0][3]);
    ans->y = add(dot3(m[1], src->x, src->y, src->z), m[1][3]);
    ans->z = add(dot3(m[2], src->x, src->y, src->z), m[2][3]);
}

void MTH_NormalTrans(MthMatrix* matrix, MthXyz* src, MthXyz* ans)
{
    Fixed32(*m)[4] = matrix->val;
    ans->x = dot3(m[0], src->x, src->y, src->z);
    ans->y = dot3(m[1], src->x, src->y, src->z);
    ans->z = dot3(m[2], src->x, src->y, src->z);
}
