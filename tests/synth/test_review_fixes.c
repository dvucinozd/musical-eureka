#include "synth.h"
#include "synth_looper.h"
#include "synth_web_keys.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../../components/synth/synth_voice_engine.h"
static synth_t s;
static synth_looper_t l;
static float out[128];
static void init(void) {
    synth_init(&s, NULL); looper_init(&l, 120); s.looper = &l;
    synth_set_delay(&s, false, 250, 0, 0); synth_set_chorus(&s, false, 0);
}
static float peak(const float *p, unsigned n) {
    float v = 0; for (unsigned i=0;i<n;i++) if (fabsf(p[i])>v) v=fabsf(p[i]); return v;
}
static void event(unsigned i, unsigned off, unsigned type) {
    l.tracks[0].events[i]=(looper_event_t){.sample_offset=off,.type=type,.note=60,.velocity=100};
}
static void render_n(unsigned n) {
    while(n) { unsigned k=n>128 ? 128:n; synth_render(&s,out,k); n-=k; }
}
static void extended(void) {
    /* Independent block partitions preserve note boundaries and wrap. */
    const unsigned blocks[]={1,64,128};
    for(unsigned bi=0;bi<3;++bi) {
        init(); l.transport_running=true; l.loop_samples=4800;
        l.tracks[0].state=LOOPER_STATE_PLAY; l.tracks[0].event_count=2;
        event(0,100,1); event(1,110,0);
        unsigned done=0;
        while(done<4900) {
            unsigned n=blocks[bi]; if(n>4900-done) n=4900-done;
            memset(out,0,sizeof(out)); looper_render_mix(&l,out,n,0);
            for(unsigned i=0;i<n && done+i<100;++i) assert(out[i]==0);
            done+=n;
        }
        assert(l.cursor_samples==100);
        memset(out,0,sizeof(out)); looper_render_mix(&l,out,1,0);
        assert(l.tracks[0].voices[0].age_samples==1);
        synth_panic(&s); render_n(10000); assert(peak(out,16)==0);
    }
    /* Input source, config-load transition, lease refresh and expiry. */
    for(unsigned ch=0;ch<=16;++ch) {
        init(); synth_set_midi_channel(&s,ch); synth_web_keys_t keys={0};
        synth_web_key(&keys,&s,60,true,1); assert(s.note_on_count==1);
        synth_web_key(&keys,&s,60,true,500001); assert(s.note_on_count==1);
        synth_web_keys_expire(&keys,&s,2100000); render_n(48000);
        assert(s.voices[0].state==VOICE_STATE_IDLE);
    }
    init(); synth_note_on(&s,60,100); render_n(128);
    uint32_t epoch=s.input_epoch; synth_set_midi_channel(&s,0); assert(s.input_epoch==epoch);
    synth_config_t cfg=s.config; cfg.midi_channel=2; synth_apply_config(&s,&cfg);
    render_n(241); assert(s.voices[0].state==VOICE_STATE_IDLE);
    synth_handle_local_note(&s,60,100,true); render_n(128);
    cfg=s.config; cfg.midi_channel=3; cfg.release_ms=10000;
    synth_apply_config(&s,&cfg); render_n(241);
    assert(s.voices[0].state==VOICE_STATE_IDLE);
    synth_set_midi_channel(&s,2);
    midi_event_t ev={.type=MIDI_EVENT_NOTE_ON,.channel=0,.note={.note=62,.velocity=100}};
    unsigned on=s.note_on_count; synth_handle_midi_event(&s,&ev); assert(on==s.note_on_count);
    ev.channel=1; synth_handle_midi_event(&s,&ev); assert(on+1==s.note_on_count);
    /* Exact sustain ramps and release retarget; identical setter is a no-op. */
    init(); synth_set_adsr(&s,1,1,1,100); synth_note_on(&s,60,100); render_n(512);
    float level=s.voices[0].env_level;
    synth_set_adsr(&s,1,1,0,100); assert(s.voices[0].env_level==level);
    render_n(239); assert(s.voices[0].env_level>0);
    synth_set_adsr(&s,1,1,0,100); assert(s.voices[0].env_remaining==1);
    render_n(1); assert(s.voices[0].env_level==0);
    synth_set_adsr(&s,1,1,1,100); render_n(240); assert(s.voices[0].env_level==level);
    synth_note_off(&s,60); render_n(64); synth_set_adsr(&s,1,1,1,1);
    render_n(48); assert(s.voices[0].state==VOICE_STATE_IDLE);
    init(); synth_set_adsr(&s,100,100,0.2f,100); synth_note_on(&s,60,100); render_n(128);
    level=s.voices[0].env_level; synth_set_adsr(&s,1,100,0.2f,100);
    assert(s.voices[0].env_level==level); render_n(48);
    assert(s.voices[0].state==VOICE_STATE_DECAY);
    render_n(2400); level=s.voices[0].env_level;
    synth_set_adsr(&s,1,1,1,100); assert(s.voices[0].env_level==level);
    render_n(48); assert(s.voices[0].state==VOICE_STATE_SUSTAIN);
    assert(s.voices[0].env_level==s.voices[0].env_target);
    cfg=s.config; cfg.sustain_ratio=0; synth_apply_config(&s,&cfg); render_n(240);
    assert(s.voices[0].env_level==0);
    /* End-of-cycle note-off and invariant at each record operation. */
    init(); looper_track_trigger(&l,0,&s); looper_record_note(&l,60,100,true,&s);
    l.cursor_samples=l.loop_samples-1; memset(out,0,sizeof(out)); looper_render_mix(&l,out,1,0);
    assert(l.cursor_samples==0 && l.tracks[0].state==LOOPER_STATE_PLAY);
    assert(l.tracks[0].event_count==2 && l.tracks[0].events[1].sample_offset==l.loop_samples);
    synth_set_arp_bpm(&s,240); assert(l.tracks[0].events[1].sample_offset==l.loop_samples);
    looper_panic(&l); assert(!l.transport_running && l.tracks[0].state==LOOPER_STATE_STOPPED);
    init(); looper_track_trigger(&l,0,&s);
    for(unsigned i=0;i<300;++i) {
        looper_record_note(&l,i%128,100,true,&s);
        assert(l.tracks[0].event_count+l.tracks[0].record_held_count<=256);
        looper_record_note(&l,i%128,0,false,&s);
        assert(l.tracks[0].event_count+l.tracks[0].record_held_count<=256);
    }
    assert(l.tracks[0].full && l.tracks[0].record_held_count==0);
    looper_track_stop(&l,0); assert(l.tracks[0].full);
    looper_track_clear(&l,0); assert(!l.tracks[0].full);
    /* Dry parity uses real track renderer, with identical voices/config/LFO. */
    for(unsigned preset=0;preset<SYNTH_PRESET_COUNT;++preset) {
        init(); synth_set_preset(&s,preset); synth_note_on(&s,60,100);
        looper_track_t *t=&l.tracks[0]; t->config=s.config; t->volume_pct=100;
        memcpy(t->voices,s.voices,sizeof(t->voices)); t->lfo=s.lfo;
        float ref[128]={0}; memset(out,0,sizeof(out));
        synth_render_voices(&s.config,s.voices,4,&s.lfo,1,0,1,ref,128);
        looper_render_mix(&l,out,128,0);
        for(unsigned i=0;i<128;++i) assert(fabsf(ref[i]-out[i])<1e-6f);
    }
    /* 32 voice stress and exact master mute. */
    init(); synth_set_master_gain(&s,50);
    for(unsigned i=0;i<16;++i) synth_note_on(&s,48+i,127);
    for(unsigned t=0;t<4;++t) {
        l.tracks[t].config=s.config;
        memcpy(l.tracks[t].voices,s.voices,4*sizeof(synth_voice_t));
    }
    for(unsigned b=0;b<300;++b) { render_n(128); for(unsigned i=0;i<128;++i) assert(isfinite(out[i]) && fabsf(out[i])<=0.55f); }
    synth_set_master_gain(&s,0); render_n(512); assert(peak(out,128)==0);
    puts("Extended input/ADSR/wrap/capacity/12-preset parity/32-voice regressions PASS");
}
static int failures;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); failures++; } } while (0)
int main(void) {
    init(); l.transport_running=true; l.tracks[0].state=LOOPER_STATE_PLAY;
    l.tracks[0].event_count=1; event(0,100,1);
    memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);
    printf("R1 event_at_100: peak_before_100=%g (expected 0)\n",peak(out,100));
    CHECK(peak(out,100)==0);
    init(); l.transport_running=true; l.tracks[0].state=LOOPER_STATE_PLAY;
    l.tracks[0].event_count=2; event(0,100,1); event(1,110,0);
    memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);
    printf("R1 on100_off110: whole_block_peak=%g (expected >0)\n",peak(out,128));
    CHECK(peak(out,128)>0);
    init(); l.transport_running=true; l.tracks[0].state=LOOPER_STATE_PLAY;
    l.tracks[0].event_count=1; event(0,0,1);
    synth_panic(&s); synth_render(&s,out,128);
    printf("R2 panic: running=%d state=%d peak_after_panic=%g (expected silence)\n",l.transport_running,l.tracks[0].state,peak(out,128));
    CHECK(peak(out,128)==0);
    init(); synth_set_midi_channel(&s,1);
    midi_event_t ev={.type=MIDI_EVENT_NOTE_ON,.channel=0,.note={.note=60,.velocity=100}};
    synth_handle_midi_event(&s,&ev); synth_render(&s,out,128);
    synth_set_midi_channel(&s,2); ev.type=MIDI_EVENT_NOTE_OFF; ev.note.velocity=0;
    synth_handle_midi_event(&s,&ev);
    for(unsigned i=0;i<1000;i++) synth_render(&s,out,128);
    printf("R3 channel_change: voice_state=%d peak=%g (expected released)\n",s.voices[0].state,peak(out,128));
    CHECK(s.voices[0].state==VOICE_STATE_IDLE);
    init(); synth_web_keys_t keys={0}; synth_set_midi_channel(&s,2);
    synth_web_key(&keys,&s,60,true,1000); synth_render(&s,out,128);
    printf("R4 web_key_channel2: on_count=%lu peak=%g (expected note)\n",(unsigned long)s.note_on_count,peak(out,128));
    CHECK(s.note_on_count==1);
    init(); l.transport_running=true; l.tracks[0].state=LOOPER_STATE_PLAY;
    l.tracks[0].event_count=1; event(0,0,1);
    memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);
    looper_track_set_mute(&l,0,true); looper_track_stop(&l,0);
    float env=l.tracks[0].voices[0].env_level;
    for(unsigned i=0;i<1000;i++) {memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);}
    printf("R5 muted_stopped: env_before=%g env_after=%g (expected 0)\n",env,l.tracks[0].voices[0].env_level);
    CHECK(l.tracks[0].voices[0].env_level==0);
    looper_track_set_mute(&l,0,false); memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);
    printf("R5 unmute_stopped: peak=%g (expected silence)\n",peak(out,128)); CHECK(peak(out,128)==0);
    init(); synth_set_adsr(&s,1,1,1,100); synth_note_on(&s,60,100);
    for(unsigned i=0;i<10;i++) synth_render(&s,out,128);
    synth_set_adsr(&s,1,1,0,100);
    for(unsigned i=0;i<1000;i++) synth_render(&s,out,128);
    printf("R6 real_time_sustain: config=%g voice_sustain=%g env=%g (expected 0)\n",s.config.sustain_ratio,s.voices[0].sustain_level,s.voices[0].env_level);
    CHECK(s.voices[0].env_level==0);
    init(); l.transport_running=true; l.tracks[0].state=LOOPER_STATE_PLAY;
    l.tracks[0].event_count=1; event(0,0,1);
    static synth_looper_t copy; copy=l;
    memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);
    float reference[128]; memcpy(reference,out,sizeof(reference));
    l=copy; l.tracks[0].config.noise_level=1; l.tracks[0].config.unison_detune=0.5f;
    memset(out,0,sizeof(out)); looper_render_mix(&l,out,128,0);
    printf("R7 timbre_noise_unison: identical_pcm=%d (expected changed output)\n",memcmp(reference,out,sizeof(out))==0);
    CHECK(memcmp(reference,out,sizeof(out))!=0);
    init(); looper_track_trigger(&l,0,&s);
    for(unsigned i=0;i<127;i++) {
        looper_record_note(&l,60,100,true,&s);
        looper_record_note(&l,60,0,false,&s);
        l.cursor_samples+=128;
    }
    looper_record_note(&l,61,100,true,&s);
    looper_record_note(&l,60,100,true,&s); l.cursor_samples+=128;
    looper_record_note(&l,60,0,false,&s);
    printf("R8 full_buffer: events=%u held=%d last_type=%u (note-off was dropped)\n",l.tracks[0].event_count,l.tracks[0].held_notes[60],l.tracks[0].events[255].type);
    CHECK(l.tracks[0].events[l.tracks[0].event_count-1].type==0);
    extended();
    printf("Review acceptance: %d failures\n",failures); return failures ? 1 : 0;
}
