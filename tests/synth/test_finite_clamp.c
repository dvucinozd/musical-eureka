#include "synth_finite_clamp.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static uint32_t bits(float x) {uint32_t b;memcpy(&b,&x,4);return b;}
static void check(float x,float bound) {
    if (!isfinite(x)) return; /* matches explicit caller precondition */
    float a=fmaxf(-bound,fminf(bound,x)),b=synth_finite_clamp(x,-bound,bound);
    assert(bits(a)==bits(b));
}
int main(void) {
    float bounds[]={.05f,1.0f};
    for(unsigned j=0;j<2;j++) {
        float b=bounds[j];
        float edge[]={0.0f,-0.0f,b,-b,nextafterf(b,0),nextafterf(b,INFINITY),
                      nextafterf(-b,0),nextafterf(-b,-INFINITY),0x1p-149f,-0x1p-149f,
                      0x1.fffffep127f,-0x1.fffffep127f};
        for(unsigned i=0;i<sizeof(edge)/sizeof(*edge);i++)check(edge[i],b);
        uint32_t random=0x12345678;
        for(unsigned i=0;i<2000000;i++) {
            random=random*1664525u+1013904223u;
            float f;memcpy(&f,&random,4);check(f,b);
        }
    }
    puts("Finite clamps bit-identical: 4M bit patterns, bounds, signed-zero, subnormal PASS");
}
