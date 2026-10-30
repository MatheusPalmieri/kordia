"""All tunable parameters in one place. This file is the spec for the C++ port.

Times in seconds unless noted. SR/HOP are fixed for the whole pipeline.
"""

SR = 22050            # internal sample rate (Hz)
HOP = 512             # block size = analysis hop (~23 ms)
ONSET_WIN = 1024      # FFT for onset flux (~46 ms): time resolution
CHROMA_WIN = 4096     # FFT for chroma (~186 ms): ~5.4 Hz/bin, enough for low E (82 Hz)

# --- input level (automatic gain before analysis) ---
# Flux and chroma compression depend on level: a quiet mic (-45 dB, 3rd live session) lost 3/4 of the strums.
AGC_TARGET_DB = -28.0         # playing level the analysis was calibrated for (synthetic scenarios ~ -28 dB)
AGC_UP, AGC_DOWN = 0.05, 0.01 # per-block tracking rates: louder fast (~0.5 s), softer slow (~2.3 s)
AGC_MIN_DB, AGC_MAX_DB = -6.0, 24.0
# (it only adapts on blocks above SILENCE_DB: room noise never pumps the gain up)

# --- onsets ---
ONSET_FMIN, ONSET_FMAX = 150.0, 5000.0   # ignores our own bass attacks (feedback risk)
ONSET_MEDIAN_FRAMES = 32                 # adaptive threshold history (~0.75 s)
ONSET_K = 1.5                            # threshold = median * K + DELTA
ONSET_DELTA = 0.25                       # absolute floor (flux units); 0.6 missed half the strums of a real mic (2026-10-29 session)
ONSET_MASK, ONSET_MASK_TIME = 0.25, 0.3       # after a strum the threshold is >= 25% of its flux for 0.3 s (its own ringing is not a strum)
ONSET_REFRACTORY = 0.10                  # min gap between onsets (a 6-string strum spreads ~70 ms; 16ths at 130 BPM = 115 ms)
SILENCE_DB = -55.0                       # frame RMS below this = silence

# --- tempo (free mode) ---
TEMPO_ENV_SECONDS = 8.0       # onset envelope history for autocorrelation
TEMPO_ACF_EVERY = 10          # frames between ACF re-estimates (~0.23 s)
TEMPO_BPM_MIN, TEMPO_BPM_MAX = 50.0, 200.0
TEMPO_PRIOR_BPM = 105.0       # log-normal prior center (resolves 8ths vs quarters)
TEMPO_PRIOR_OCTAVES = 0.7     # prior width (std, in octaves)
TEMPO_MIN_ONSETS = 6          # onsets needed before first estimate
PLL_WINDOW = 0.2              # onset within ±20% of a period corrects the PLL
PLL_ALPHA = 0.35              # phase correction gain
PLL_BETA = 0.10               # period correction gain
TEMPO_RESET_DEV = 0.12        # ACF disagrees with PLL period by >12% for a while -> reset period
TEMPO_CONF_BEATS = 8          # confidence = share of last N beats with an aligned onset
BEATS_PER_BAR = 4

# --- chords ---
CHROMA_FMIN, CHROMA_FMAX = 60.0, 2500.0
CHROMA_GAMMA = 100.0          # log compression log(1 + gamma*mag)
TEMPLATE_HARMONICS = 6
TEMPLATE_DECAY = 0.6          # weight of harmonic h = decay**(h-1)
HMM_BETA = 18.0               # likelihood sharpness exp(beta * cosine)
HMM_N_SCORE = 0.55            # cosine-equivalent score for "no chord"
HMM_P_CHANGE = 0.002          # per-frame change probability (persistence)
HMM_P_CHANGE_ONSET = 0.02     # right after an onset
HMM_P_CHANGE_STRONG = 0.08    # right after an onset on a strong beat (1 or 3)
HMM_ONSET_BOOST_TIME = 0.25   # how long the boost lasts after the onset
KEY_PRIOR_OUT = 0.3           # non-diatonic chords: less likely, never blocked
CONF_BETA = 25.0              # confidence = smoothed per-frame softmax(CONF_BETA * cosine) at the HMM's chord;
CONF_SMOOTH = 0.3             #   the HMM posterior itself saturates at ~1.0 and is useless as confidence

# --- decision ---
CONF_HIGH = 0.70              # confidence to accept a chord (CONFIANTE)
CONF_LOW = 0.45               # below this: DUVIDA
CHANGE_AFTER_ONSET = 0.35     # reactive change only this soon after a strum
DOUBT_TO_FALLBACK_BARS = 1.0  # doubt longer than this -> FALLBACK
STOP_BEATS = 1.5              # no onset for max(STOP_BEATS beats, STOP_IOI_FACTOR*max recent IOI) -> stopped
STOP_IOI_FACTOR = 1.25
STOP_DEFAULT_SECONDS = 1.0    # stop timeout when tempo is unknown
FORGET_BARS = 2.0             # silence longer than this -> forget progression and tempo
FADE_TIME = 0.4
LOCK_CONFIRM_BEATS = 0.5      # after a predicted change, wait this before checking the audio
LOCK_MAX_MISSES = 3           # weak disagreements (another real chord, low confidence) in a row before unlocking
LOCK_CONTRA_BEATS = 0.5       # a confident disagreement must last this long to override the learned pattern
LOCK_LATE_BEATS = 1.0         # hearing the previous chord up to 1 beat after a predicted change = musician late
CYCLE_MIN, CYCLE_MAX = 2, 8   # cycle length in chords
# "manter o padrão" (hold): once the cycle is locked it is trusted over the audio
HOLD_STOP_BARS = 1.25         # with a held pattern, stop only after 5 beats of silence (late entries keep playing)
HOLD_CONTRA_BARS = 1.5         # (2.0 tested: key change without a pause 0.88 vs 0.90; mistakes scenario equal)        # confident disagreement this long = the musician really changed: relearn
METRO_LATE_BEATS = 0.5        # metronome mode: change heard up to this after a strong beat applies now

# --- dynamics ---
DYN_DB_LOW, DYN_DB_HIGH = -45.0, -12.0   # strum loudness mapped to velocity range
DYN_VEL_LOW, DYN_VEL_HIGH = 60, 120    # floor raised: soft playing must still be clearly audible
DYN_SMOOTH = 0.25             # EMA weight per strum (smoothing over ~4 strums)
DYN_SILENCE_DECAY = 1.5       # expression decay time constant while not strumming (pause "abaixa")

# --- accompaniment ---
BASS_LOW = 36                 # bass root in MIDI 36..47 (C2..B2)
PAD_ROOT_LOW = 48             # pad open voicing: root in C3..B3, fifth, octave, tenth (third + 12), shimmer (+24)
PAD_SHIMMER = 0.35            # relative velocity of the +2-octave shimmer note

# --- sound (synth.py) ---
PAD_ATTACK = 0.9              # s, smooth swell
PAD_RELEASE = 2.2             # s: chords crossfade into each other (worship pad)
PAD_CUTOFF = 1800.0           # Hz, low-pass on the pad bus (warm, not organ-bright)
PAD_LEVEL = 0.16              # per note (3 detuned saws averaged)
PAD_DRY, PAD_WET = 0.75, 0.6  # reverb mix
REVERB_ROOM, REVERB_DAMP = 0.86, 0.35
BASS_LEVEL = 0.45
BASS_RELEASE = 0.15
CLICK_LEVEL = 0.5
STOP_FADE = 0.6               # s: release used when the musician stops (short, not the long pad release)
