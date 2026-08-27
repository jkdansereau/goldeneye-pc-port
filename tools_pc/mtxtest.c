/* Standalone numerical test of the fast3d matrix pipeline conventions.
 * Replicates: GE guPerspectiveF (swapped), guLookAtF, guRotateF, guMtxF2L
 * packing, gfx_sp_matrix unpacking + MUL order, and the row-vector vertex
 * transform in gfx_pc.cpp. Compares against the standard column-vector
 * pipeline P_std * MV * v for two scenes: gun barrel (known-good render)
 * and rareware logo (D72.3 off-screen bug).
 */
#include <math.h>
#include <stdio.h>
#include <stdint.h>

typedef float f32;

/* ---- GE libultra functions (verbatim math) ---- */

static void guMtxIdentF(float mf[4][4]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) mf[i][j] = (i == j);
}

/* GE's SWAPPED perspective: mf[2][3]=-1, mf[3][2]=2nf/(n-f) */
static void guPerspectiveF_GE(float mf[4][4], float fovy, float aspect, float near_, float far_) {
    float cot;
    guMtxIdentF(mf);
    fovy *= 3.1415926f / 180.0f;
    cot = cosf(fovy / 2) / sinf(fovy / 2);
    mf[0][0] = cot / aspect;
    mf[1][1] = cot;
    mf[2][2] = (near_ + far_) / (near_ - far_);
    mf[2][3] = -1;
    mf[3][2] = (2 * near_ * far_) / (near_ - far_);
    mf[3][3] = 0;
}

/* standard libultra perspective (for the "true" pipeline) */
static void guPerspectiveF_STD(float mf[4][4], float fovy, float aspect, float near_, float far_) {
    float cot;
    guMtxIdentF(mf);
    fovy *= 3.1415926f / 180.0f;
    cot = cosf(fovy / 2) / sinf(fovy / 2);
    mf[0][0] = cot / aspect;
    mf[1][1] = cot;
    mf[2][2] = (near_ + far_) / (near_ - far_);
    mf[2][3] = (2 * near_ * far_) / (near_ - far_);
    mf[3][2] = -1;
    mf[3][3] = 0;
}

static void guLookAtF(float mf[4][4], float xEye, float yEye, float zEye,
                      float xAt, float yAt, float zAt,
                      float xUp, float yUp, float zUp) {
    float len, xLook, yLook, zLook, xRight, yRight, zRight;
    guMtxIdentF(mf);
    xLook = xAt - xEye; yLook = yAt - yEye; zLook = zAt - zEye;
    len = -1.0f / sqrtf(xLook*xLook + yLook*yLook + zLook*zLook);
    xLook *= len; yLook *= len; zLook *= len;
    xRight = yUp * zLook - zUp * yLook;
    yRight = zUp * xLook - xUp * zLook;
    zRight = xUp * yLook - yUp * xLook;
    len = 1.0f / sqrtf(xRight*xRight + yRight*yRight + zRight*zRight);
    xRight *= len; yRight *= len; zRight *= len;
    xUp = yLook * zRight - zLook * yRight;
    yUp = zLook * xRight - xLook * zRight;
    zUp = xLook * yRight - yLook * xRight;
    len = 1.0f / sqrtf(xUp*xUp + yUp*yUp + zUp*zUp);
    xUp *= len; yUp *= len; zUp *= len;
    mf[0][0] = xRight; mf[1][0] = yRight; mf[2][0] = zRight;
    mf[3][0] = -(xEye * xRight + yEye * yRight + zEye * zRight);
    mf[0][1] = xUp; mf[1][1] = yUp; mf[2][1] = zUp;
    mf[3][1] = -(xEye * xUp + yEye * yUp + zEye * zUp);
    mf[0][2] = xLook; mf[1][2] = yLook; mf[2][2] = zLook;
    mf[3][2] = -(xEye * xLook + yEye * yLook + zEye * zLook);
    mf[0][3] = 0; mf[1][3] = 0; mf[2][3] = 0; mf[3][3] = 1;
}

static void guRotateF(float mf[4][4], float a, float x, float y, float z) {
    static float dtor = 3.1415926f / 180.0f;
    float sine, cosine, ab, bc, ca, t;
    float l = sqrtf(x*x + y*y + z*z);
    if (l != 0) { x /= l; y /= l; z /= l; }
    a *= dtor;
    sine = sinf(a); cosine = cosf(a);
    t = (1 - cosine);
    ab = x * y * t; bc = y * z * t; ca = z * x * t;
    guMtxIdentF(mf);
    t = x * x;
    mf[0][0] = t + cosine * (1 - t);
    mf[2][1] = bc - x * sine;
    mf[1][2] = bc + x * sine;
    t = y * y;
    mf[1][1] = t + cosine * (1 - t);
    mf[2][0] = ca + y * sine;
    mf[0][2] = ca - y * sine;
    t = z * z;
    mf[2][2] = t + cosine * (1 - t);
    mf[1][0] = ab - z * sine;
    mf[0][1] = ab + z * sine;
}

/* ---- guMtxF2L pack / gfx_sp_matrix unpack (bit-exact path) ---- */

static void mtxf2l(const float mf[4][4], int32_t m[16]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 2; j++) {
            int e1 = (int)(mf[i][j*2] * 65536.0f);
            int e2 = (int)(mf[i][j*2+1] * 65536.0f);
            /* real guMtxF2L uses *(ai++)/*(af++): slot = i*2+j */
            m[i*2 + j]      = (e1 & 0xffff0000) | ((e2 >> 16) & 0xffff);
            m[8 + i*2 + j]  = ((e1 << 16) & 0xffff0000) | (e2 & 0xffff);
        }
}

static void sp_matrix_unpack(const int32_t addr[16], float matrix[4][4]) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j += 2) {
            int32_t int_part = addr[i * 2 + j / 2];
            uint32_t frac_part = addr[8 + i * 2 + j / 2];
            matrix[i][j] = (int32_t)((int_part & 0xffff0000) | (frac_part >> 16)) / 65536.0f;
            matrix[i][j + 1] = (int32_t)((int_part << 16) | (frac_part & 0xffff)) / 65536.0f;
        }
}

/* ---- gfx_pc.cpp conventions ---- */

static void mmul(float res[4][4], const float a[4][4], const float b[4][4]) {
    float tmp[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            tmp[i][j] = a[i][0]*b[0][j] + a[i][1]*b[1][j] + a[i][2]*b[2][j] + a[i][3]*b[3][j];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) res[i][j] = tmp[i][j];
}

/* row-vector vertex transform exactly as in gfx_sp_vertex */
static void xform_row(const float MP[4][4], const float v[3], float c[4]) {
    c[0] = v[0]*MP[0][0] + v[1]*MP[1][0] + v[2]*MP[2][0] + MP[3][0];
    c[1] = v[0]*MP[0][1] + v[1]*MP[1][1] + v[2]*MP[2][1] + MP[3][1];
    c[2] = v[0]*MP[0][2] + v[1]*MP[1][2] + v[2]*MP[2][2] + MP[3][2];
    c[3] = v[0]*MP[0][3] + v[1]*MP[1][3] + v[2]*MP[2][3] + MP[3][3];
}

/* standard column-vector transform c = A*B*v */
static void xform_col(const float A[4][4], const float B[4][4], const float v[3], float c[4]) {
    float M[4][4];
    mmul(M, A, B);
    for (int i = 0; i < 4; i++)
        c[i] = M[i][0]*v[0] + M[i][1]*v[1] + M[i][2]*v[2] + M[i][3];
}

static void report(const char *tag, const float c[4]) {
    float xw = c[3] != 0 ? c[0]/c[3] : 1e9f;
    float yw = c[3] != 0 ? c[1]/c[3] : 1e9f;
    printf("  %-34s clip=(%12.1f,%12.1f,%12.1f,%12.1f) x/w=%8.4f y/w=%8.4f %s\n",
           tag, c[0], c[1], c[2], c[3], xw, yw,
           (fabsf(xw) <= 1 && fabsf(yw) <= 1) ? "ON-SCREEN" : "off-screen");
}

/* pack+unpack round-trip so we exercise the exact bit path */
static void rt(const float mf[4][4], float out[4][4]) {
    int32_t m[16];
    mtxf2l(mf, m);
    sp_matrix_unpack(m, out);
}

int main(void) {
    float Pge[4][4], Pstd[4][4], MVla[4][4], Rot[4][4];
    float MPours[4][4], MPalt[4][4], Mtot[4][4];
    float c[4];

    printf("== SCENE 1: gun barrel (eye 1758.3,220,684.3 dir -0.97,0,0.24 up 0,1,0; fov46 n10 f10000) ==\n");
    guPerspectiveF_GE(Pge, 46.0f, 320.0f/240.0f, 10.0f, 10000.0f);
    guPerspectiveF_STD(Pstd, 46.0f, 320.0f/240.0f, 10.0f, 10000.0f);
    guLookAtF(MVla, 1758.2957f, 220.0f, 684.28143f,
              1758.2957f - 0.97f, 220.0f, 684.28143f + 0.24f, 0, 1, 0);
    rt(Pge, Pge); rt(MVla, MVla);

    float v[3] = { -57.3f, -20.0f, 16.3f }; /* sample point offset from target */
    mmul(MPours, MVla, Pge);                 /* our MP for LOAD-only */
    xform_row(MPours, v, c);
    report("OURS row: v.MV.P_ge", c);
    xform_col(Pstd, MVla, v, c);
    report("TRUE col: P_std*MV*v", c);

    printf("\n== SCENE 2: rareware logo (eye 0,0,880 at 0,0,879 up 0,1,0; fov60 n100 f5000; rotY -40deg) ==\n");
    guPerspectiveF_GE(Pge, 60.0f, 320.0f/240.0f, 100.0f, 5000.0f);
    guPerspectiveF_STD(Pstd, 60.0f, 320.0f/240.0f, 100.0f, 5000.0f);
    guLookAtF(MVla, 0, 0, 880.0f, 0, 0, 879.0f, 0, 1, 0);
    guRotateF(Rot, -40.0f, 0, 1, 0);
    rt(Pge, Pge); rt(MVla, MVla); rt(Rot, Rot);

    float lv[3] = { 55.0f, 61.0f, 15.0f };  /* verts4258[0].ob */
    mmul(MPours, Rot, MVla);                 /* our code: new = matrix*old */
    mmul(MPours, MPours, Pge);
    xform_row(MPours, lv, c);
    report("OURS MUL (Rot*MV) then P", c);

    mmul(MPalt, MVla, Rot);                  /* alt: new = old*matrix */
    mmul(MPalt, MPalt, Pge);
    xform_row(MPalt, lv, c);
    report("ALT  MUL (MV*Rot) then P", c);

    /* true standard pipeline: P_std * (MVla*Rot) * v  [Rot applied first] */
    mmul(Mtot, MVla, Rot);
    xform_col(Pstd, Mtot, lv, c);
    report("TRUE col: P_std*MV*Rot*v", c);

    /* and the other hardware-MUL interpretation: P_std * (Rot*MV) * v */
    mmul(Mtot, Rot, MVla);
    xform_col(Pstd, Mtot, lv, c);
    report("TRUE col: P_std*Rot*MV*v", c);

    return 0;
}
