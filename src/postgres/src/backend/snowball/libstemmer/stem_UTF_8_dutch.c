/* Generated by Snowball 2.2.0 - https://snowballstem.org/ */

#include "header.h"

#ifdef __cplusplus
extern "C" {
#endif
extern int dutch_UTF_8_stem(struct SN_env * z);
#ifdef __cplusplus
}
#endif
static int r_standard_suffix(struct SN_env * z);
static int r_undouble(struct SN_env * z);
static int r_R2(struct SN_env * z);
static int r_R1(struct SN_env * z);
static int r_mark_regions(struct SN_env * z);
static int r_en_ending(struct SN_env * z);
static int r_e_ending(struct SN_env * z);
static int r_postlude(struct SN_env * z);
static int r_prelude(struct SN_env * z);
#ifdef __cplusplus
extern "C" {
#endif


extern struct SN_env * dutch_UTF_8_create_env(void);
extern void dutch_UTF_8_close_env(struct SN_env * z);


#ifdef __cplusplus
}
#endif
static const symbol s_0_1[2] = { 0xC3, 0xA1 };
static const symbol s_0_2[2] = { 0xC3, 0xA4 };
static const symbol s_0_3[2] = { 0xC3, 0xA9 };
static const symbol s_0_4[2] = { 0xC3, 0xAB };
static const symbol s_0_5[2] = { 0xC3, 0xAD };
static const symbol s_0_6[2] = { 0xC3, 0xAF };
static const symbol s_0_7[2] = { 0xC3, 0xB3 };
static const symbol s_0_8[2] = { 0xC3, 0xB6 };
static const symbol s_0_9[2] = { 0xC3, 0xBA };
static const symbol s_0_10[2] = { 0xC3, 0xBC };

static const struct among a_0[11] =
{
{ 0, 0, -1, 6, 0},
{ 2, s_0_1, 0, 1, 0},
{ 2, s_0_2, 0, 1, 0},
{ 2, s_0_3, 0, 2, 0},
{ 2, s_0_4, 0, 2, 0},
{ 2, s_0_5, 0, 3, 0},
{ 2, s_0_6, 0, 3, 0},
{ 2, s_0_7, 0, 4, 0},
{ 2, s_0_8, 0, 4, 0},
{ 2, s_0_9, 0, 5, 0},
{ 2, s_0_10, 0, 5, 0}
};

static const symbol s_1_1[1] = { 'I' };
static const symbol s_1_2[1] = { 'Y' };

static const struct among a_1[3] =
{
{ 0, 0, -1, 3, 0},
{ 1, s_1_1, 0, 2, 0},
{ 1, s_1_2, 0, 1, 0}
};

static const symbol s_2_0[2] = { 'd', 'd' };
static const symbol s_2_1[2] = { 'k', 'k' };
static const symbol s_2_2[2] = { 't', 't' };

static const struct among a_2[3] =
{
{ 2, s_2_0, -1, -1, 0},
{ 2, s_2_1, -1, -1, 0},
{ 2, s_2_2, -1, -1, 0}
};

static const symbol s_3_0[3] = { 'e', 'n', 'e' };
static const symbol s_3_1[2] = { 's', 'e' };
static const symbol s_3_2[2] = { 'e', 'n' };
static const symbol s_3_3[5] = { 'h', 'e', 'd', 'e', 'n' };
static const symbol s_3_4[1] = { 's' };

static const struct among a_3[5] =
{
{ 3, s_3_0, -1, 2, 0},
{ 2, s_3_1, -1, 3, 0},
{ 2, s_3_2, -1, 2, 0},
{ 5, s_3_3, 2, 1, 0},
{ 1, s_3_4, -1, 3, 0}
};

static const symbol s_4_0[3] = { 'e', 'n', 'd' };
static const symbol s_4_1[2] = { 'i', 'g' };
static const symbol s_4_2[3] = { 'i', 'n', 'g' };
static const symbol s_4_3[4] = { 'l', 'i', 'j', 'k' };
static const symbol s_4_4[4] = { 'b', 'a', 'a', 'r' };
static const symbol s_4_5[3] = { 'b', 'a', 'r' };

static const struct among a_4[6] =
{
{ 3, s_4_0, -1, 1, 0},
{ 2, s_4_1, -1, 2, 0},
{ 3, s_4_2, -1, 1, 0},
{ 4, s_4_3, -1, 3, 0},
{ 4, s_4_4, -1, 4, 0},
{ 3, s_4_5, -1, 5, 0}
};

static const symbol s_5_0[2] = { 'a', 'a' };
static const symbol s_5_1[2] = { 'e', 'e' };
static const symbol s_5_2[2] = { 'o', 'o' };
static const symbol s_5_3[2] = { 'u', 'u' };

static const struct among a_5[4] =
{
{ 2, s_5_0, -1, -1, 0},
{ 2, s_5_1, -1, -1, 0},
{ 2, s_5_2, -1, -1, 0},
{ 2, s_5_3, -1, -1, 0}
};

static const unsigned char g_v[] = { 17, 65, 16, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128 };

static const unsigned char g_v_I[] = { 1, 0, 0, 17, 65, 16, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128 };

static const unsigned char g_v_j[] = { 17, 67, 16, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 128 };

static const symbol s_0[] = { 'a' };
static const symbol s_1[] = { 'e' };
static const symbol s_2[] = { 'i' };
static const symbol s_3[] = { 'o' };
static const symbol s_4[] = { 'u' };
static const symbol s_5[] = { 'Y' };
static const symbol s_6[] = { 'I' };
static const symbol s_7[] = { 'Y' };
static const symbol s_8[] = { 'y' };
static const symbol s_9[] = { 'i' };
static const symbol s_10[] = { 'g', 'e', 'm' };
static const symbol s_11[] = { 'h', 'e', 'i', 'd' };
static const symbol s_12[] = { 'h', 'e', 'i', 'd' };
static const symbol s_13[] = { 'e', 'n' };
static const symbol s_14[] = { 'i', 'g' };

static int r_prelude(struct SN_env * z) {
    int among_var;
    {   int c_test1 = z->c;
        while(1) {
            int c2 = z->c;
            z->bra = z->c;
            if (z->c + 1 >= z->l || z->p[z->c + 1] >> 5 != 5 || !((340306450 >> (z->p[z->c + 1] & 0x1f)) & 1)) among_var = 6; else
            among_var = find_among(z, a_0, 11);
            if (!(among_var)) goto lab0;
            z->ket = z->c;
            switch (among_var) {
                case 1:
                    {   int ret = slice_from_s(z, 1, s_0);
                        if (ret < 0) return ret;
                    }
                    break;
                case 2:
                    {   int ret = slice_from_s(z, 1, s_1);
                        if (ret < 0) return ret;
                    }
                    break;
                case 3:
                    {   int ret = slice_from_s(z, 1, s_2);
                        if (ret < 0) return ret;
                    }
                    break;
                case 4:
                    {   int ret = slice_from_s(z, 1, s_3);
                        if (ret < 0) return ret;
                    }
                    break;
                case 5:
                    {   int ret = slice_from_s(z, 1, s_4);
                        if (ret < 0) return ret;
                    }
                    break;
                case 6:
                    {   int ret = skip_utf8(z->p, z->c, z->l, 1);
                        if (ret < 0) goto lab0;
                        z->c = ret;
                    }
                    break;
            }
            continue;
        lab0:
            z->c = c2;
            break;
        }
        z->c = c_test1;
    }
    {   int c3 = z->c;
        z->bra = z->c;
        if (z->c == z->l || z->p[z->c] != 'y') { z->c = c3; goto lab1; }
        z->c++;
        z->ket = z->c;
        {   int ret = slice_from_s(z, 1, s_5);
            if (ret < 0) return ret;
        }
    lab1:
        ;
    }
    while(1) {
        int c4 = z->c;
        while(1) {
            int c5 = z->c;
            if (in_grouping_U(z, g_v, 97, 232, 0)) goto lab3;
            z->bra = z->c;
            {   int c6 = z->c;
                if (z->c == z->l || z->p[z->c] != 'i') goto lab5;
                z->c++;
                z->ket = z->c;
                if (in_grouping_U(z, g_v, 97, 232, 0)) goto lab5;
                {   int ret = slice_from_s(z, 1, s_6);
                    if (ret < 0) return ret;
                }
                goto lab4;
            lab5:
                z->c = c6;
                if (z->c == z->l || z->p[z->c] != 'y') goto lab3;
                z->c++;
                z->ket = z->c;
                {   int ret = slice_from_s(z, 1, s_7);
                    if (ret < 0) return ret;
                }
            }
        lab4:
            z->c = c5;
            break;
        lab3:
            z->c = c5;
            {   int ret = skip_utf8(z->p, z->c, z->l, 1);
                if (ret < 0) goto lab2;
                z->c = ret;
            }
        }
        continue;
    lab2:
        z->c = c4;
        break;
    }
    return 1;
}

static int r_mark_regions(struct SN_env * z) {
    z->I[1] = z->l;
    z->I[0] = z->l;
    {   
        int ret = out_grouping_U(z, g_v, 97, 232, 1);
        if (ret < 0) return 0;
        z->c += ret;
    }
    {   
        int ret = in_grouping_U(z, g_v, 97, 232, 1);
        if (ret < 0) return 0;
        z->c += ret;
    }
    z->I[1] = z->c;
    
    if (!(z->I[1] < 3)) goto lab0;
    z->I[1] = 3;
lab0:
    {   
        int ret = out_grouping_U(z, g_v, 97, 232, 1);
        if (ret < 0) return 0;
        z->c += ret;
    }
    {   
        int ret = in_grouping_U(z, g_v, 97, 232, 1);
        if (ret < 0) return 0;
        z->c += ret;
    }
    z->I[0] = z->c;
    return 1;
}

static int r_postlude(struct SN_env * z) {
    int among_var;
    while(1) {
        int c1 = z->c;
        z->bra = z->c;
        if (z->c >= z->l || (z->p[z->c + 0] != 73 && z->p[z->c + 0] != 89)) among_var = 3; else
        among_var = find_among(z, a_1, 3);
        if (!(among_var)) goto lab0;
        z->ket = z->c;
        switch (among_var) {
            case 1:
                {   int ret = slice_from_s(z, 1, s_8);
                    if (ret < 0) return ret;
                }
                break;
            case 2:
                {   int ret = slice_from_s(z, 1, s_9);
                    if (ret < 0) return ret;
                }
                break;
            case 3:
                {   int ret = skip_utf8(z->p, z->c, z->l, 1);
                    if (ret < 0) goto lab0;
                    z->c = ret;
                }
                break;
        }
        continue;
    lab0:
        z->c = c1;
        break;
    }
    return 1;
}

static int r_R1(struct SN_env * z) {
    if (!(z->I[1] <= z->c)) return 0;
    return 1;
}

static int r_R2(struct SN_env * z) {
    if (!(z->I[0] <= z->c)) return 0;
    return 1;
}

static int r_undouble(struct SN_env * z) {
    {   int m_test1 = z->l - z->c;
        if (z->c - 1 <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((1050640 >> (z->p[z->c - 1] & 0x1f)) & 1)) return 0;
        if (!(find_among_b(z, a_2, 3))) return 0;
        z->c = z->l - m_test1;
    }
    z->ket = z->c;
    {   int ret = skip_b_utf8(z->p, z->c, z->lb, 1);
        if (ret < 0) return 0;
        z->c = ret;
    }
    z->bra = z->c;
    {   int ret = slice_del(z);
        if (ret < 0) return ret;
    }
    return 1;
}

static int r_e_ending(struct SN_env * z) {
    z->I[2] = 0;
    z->ket = z->c;
    if (z->c <= z->lb || z->p[z->c - 1] != 'e') return 0;
    z->c--;
    z->bra = z->c;
    {   int ret = r_R1(z);
        if (ret <= 0) return ret;
    }
    {   int m_test1 = z->l - z->c;
        if (out_grouping_b_U(z, g_v, 97, 232, 0)) return 0;
        z->c = z->l - m_test1;
    }
    {   int ret = slice_del(z);
        if (ret < 0) return ret;
    }
    z->I[2] = 1;
    {   int ret = r_undouble(z);
        if (ret <= 0) return ret;
    }
    return 1;
}

static int r_en_ending(struct SN_env * z) {
    {   int ret = r_R1(z);
        if (ret <= 0) return ret;
    }
    {   int m1 = z->l - z->c; (void)m1;
        if (out_grouping_b_U(z, g_v, 97, 232, 0)) return 0;
        z->c = z->l - m1;
        {   int m2 = z->l - z->c; (void)m2;
            if (!(eq_s_b(z, 3, s_10))) goto lab0;
            return 0;
        lab0:
            z->c = z->l - m2;
        }
    }
    {   int ret = slice_del(z);
        if (ret < 0) return ret;
    }
    {   int ret = r_undouble(z);
        if (ret <= 0) return ret;
    }
    return 1;
}

static int r_standard_suffix(struct SN_env * z) {
    int among_var;
    {   int m1 = z->l - z->c; (void)m1;
        z->ket = z->c;
        if (z->c <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((540704 >> (z->p[z->c - 1] & 0x1f)) & 1)) goto lab0;
        among_var = find_among_b(z, a_3, 5);
        if (!(among_var)) goto lab0;
        z->bra = z->c;
        switch (among_var) {
            case 1:
                {   int ret = r_R1(z);
                    if (ret == 0) goto lab0;
                    if (ret < 0) return ret;
                }
                {   int ret = slice_from_s(z, 4, s_11);
                    if (ret < 0) return ret;
                }
                break;
            case 2:
                {   int ret = r_en_ending(z);
                    if (ret == 0) goto lab0;
                    if (ret < 0) return ret;
                }
                break;
            case 3:
                {   int ret = r_R1(z);
                    if (ret == 0) goto lab0;
                    if (ret < 0) return ret;
                }
                if (out_grouping_b_U(z, g_v_j, 97, 232, 0)) goto lab0;
                {   int ret = slice_del(z);
                    if (ret < 0) return ret;
                }
                break;
        }
    lab0:
        z->c = z->l - m1;
    }
    {   int m2 = z->l - z->c; (void)m2;
        {   int ret = r_e_ending(z);
            if (ret < 0) return ret;
        }
        z->c = z->l - m2;
    }
    {   int m3 = z->l - z->c; (void)m3;
        z->ket = z->c;
        if (!(eq_s_b(z, 4, s_12))) goto lab1;
        z->bra = z->c;
        {   int ret = r_R2(z);
            if (ret == 0) goto lab1;
            if (ret < 0) return ret;
        }
        {   int m4 = z->l - z->c; (void)m4;
            if (z->c <= z->lb || z->p[z->c - 1] != 'c') goto lab2;
            z->c--;
            goto lab1;
        lab2:
            z->c = z->l - m4;
        }
        {   int ret = slice_del(z);
            if (ret < 0) return ret;
        }
        z->ket = z->c;
        if (!(eq_s_b(z, 2, s_13))) goto lab1;
        z->bra = z->c;
        {   int ret = r_en_ending(z);
            if (ret == 0) goto lab1;
            if (ret < 0) return ret;
        }
    lab1:
        z->c = z->l - m3;
    }
    {   int m5 = z->l - z->c; (void)m5;
        z->ket = z->c;
        if (z->c - 1 <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((264336 >> (z->p[z->c - 1] & 0x1f)) & 1)) goto lab3;
        among_var = find_among_b(z, a_4, 6);
        if (!(among_var)) goto lab3;
        z->bra = z->c;
        switch (among_var) {
            case 1:
                {   int ret = r_R2(z);
                    if (ret == 0) goto lab3;
                    if (ret < 0) return ret;
                }
                {   int ret = slice_del(z);
                    if (ret < 0) return ret;
                }
                {   int m6 = z->l - z->c; (void)m6;
                    z->ket = z->c;
                    if (!(eq_s_b(z, 2, s_14))) goto lab5;
                    z->bra = z->c;
                    {   int ret = r_R2(z);
                        if (ret == 0) goto lab5;
                        if (ret < 0) return ret;
                    }
                    {   int m7 = z->l - z->c; (void)m7;
                        if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab6;
                        z->c--;
                        goto lab5;
                    lab6:
                        z->c = z->l - m7;
                    }
                    {   int ret = slice_del(z);
                        if (ret < 0) return ret;
                    }
                    goto lab4;
                lab5:
                    z->c = z->l - m6;
                    {   int ret = r_undouble(z);
                        if (ret == 0) goto lab3;
                        if (ret < 0) return ret;
                    }
                }
            lab4:
                break;
            case 2:
                {   int ret = r_R2(z);
                    if (ret == 0) goto lab3;
                    if (ret < 0) return ret;
                }
                {   int m8 = z->l - z->c; (void)m8;
                    if (z->c <= z->lb || z->p[z->c - 1] != 'e') goto lab7;
                    z->c--;
                    goto lab3;
                lab7:
                    z->c = z->l - m8;
                }
                {   int ret = slice_del(z);
                    if (ret < 0) return ret;
                }
                break;
            case 3:
                {   int ret = r_R2(z);
                    if (ret == 0) goto lab3;
                    if (ret < 0) return ret;
                }
                {   int ret = slice_del(z);
                    if (ret < 0) return ret;
                }
                {   int ret = r_e_ending(z);
                    if (ret == 0) goto lab3;
                    if (ret < 0) return ret;
                }
                break;
            case 4:
                {   int ret = r_R2(z);
                    if (ret == 0) goto lab3;
                    if (ret < 0) return ret;
                }
                {   int ret = slice_del(z);
                    if (ret < 0) return ret;
                }
                break;
            case 5:
                {   int ret = r_R2(z);
                    if (ret == 0) goto lab3;
                    if (ret < 0) return ret;
                }
                if (!(z->I[2])) goto lab3;
                {   int ret = slice_del(z);
                    if (ret < 0) return ret;
                }
                break;
        }
    lab3:
        z->c = z->l - m5;
    }
    {   int m9 = z->l - z->c; (void)m9;
        if (out_grouping_b_U(z, g_v_I, 73, 232, 0)) goto lab8;
        {   int m_test10 = z->l - z->c;
            if (z->c - 1 <= z->lb || z->p[z->c - 1] >> 5 != 3 || !((2129954 >> (z->p[z->c - 1] & 0x1f)) & 1)) goto lab8;
            if (!(find_among_b(z, a_5, 4))) goto lab8;
            if (out_grouping_b_U(z, g_v, 97, 232, 0)) goto lab8;
            z->c = z->l - m_test10;
        }
        z->ket = z->c;
        {   int ret = skip_b_utf8(z->p, z->c, z->lb, 1);
            if (ret < 0) goto lab8;
            z->c = ret;
        }
        z->bra = z->c;
        {   int ret = slice_del(z);
            if (ret < 0) return ret;
        }
    lab8:
        z->c = z->l - m9;
    }
    return 1;
}

extern int dutch_UTF_8_stem(struct SN_env * z) {
    {   int c1 = z->c;
        {   int ret = r_prelude(z);
            if (ret < 0) return ret;
        }
        z->c = c1;
    }
    {   int c2 = z->c;
        {   int ret = r_mark_regions(z);
            if (ret < 0) return ret;
        }
        z->c = c2;
    }
    z->lb = z->c; z->c = z->l;

    
    {   int ret = r_standard_suffix(z);
        if (ret < 0) return ret;
    }
    z->c = z->lb;
    {   int c3 = z->c;
        {   int ret = r_postlude(z);
            if (ret < 0) return ret;
        }
        z->c = c3;
    }
    return 1;
}

extern struct SN_env * dutch_UTF_8_create_env(void) { return SN_create_env(0, 3); }

extern void dutch_UTF_8_close_env(struct SN_env * z) { SN_close_env(z, 0); }

