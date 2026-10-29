"""Chord names, pitch classes, guitar voicings, templates and key prior."""
import numpy as np

from . import params as P

NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
_ALIASES = {"Db": 1, "D#": 3, "Gb": 6, "G#": 8, "A#": 10, "Cb": 11, "Fb": 4, "E#": 5, "B#": 0}
N = "N"  # no chord

# chord vocabulary index: 0..11 major, 12..23 minor, 24 = N
LABELS = [n for n in NOTE_NAMES] + [n + "m" for n in NOTE_NAMES] + [N]


def note_pc(name):
    if name in NOTE_NAMES:
        return NOTE_NAMES.index(name)
    return _ALIASES[name]


def parse_chord(label):
    """'Am', 'C', 'F#m', 'Am/G', 'G7' -> (root_pc, is_minor). Extensions and slash bass are dropped (canonical triad)."""
    label = label.split("/")[0]
    root = label[:2] if len(label) > 1 and label[1] in "#b" else label[:1]
    rest = label[len(root):]
    minor = rest.startswith("m") and not rest.startswith("maj")
    return note_pc(root), minor


def canonical(label):
    """Label in vocabulary form ('Am/G' -> 'Am', 'G7' -> 'G')."""
    if label in (N, "P"):
        return label
    r, m = parse_chord(label)
    return LABELS[r + 12 * m]


def transpose(label, n):
    r, m = parse_chord(label)
    return LABELS[(r + n) % 12 + 12 * m]


def chord_index(label):
    return LABELS.index(canonical(label))


def chord_pcs(idx):
    """Pitch classes (root, third, fifth) of vocabulary index."""
    root, minor = idx % 12, idx >= 12
    return [root, (root + (3 if minor else 4)) % 12, (root + 7) % 12]


def templates():
    """(24, 12) unit-norm chroma templates including harmonics."""
    T = np.zeros((24, 12))
    for i in range(24):
        for tone in chord_pcs(i):
            for h in range(1, P.TEMPLATE_HARMONICS + 1):
                T[i, (tone + int(round(12 * np.log2(h)))) % 12] += P.TEMPLATE_DECAY ** (h - 1)
    return T / np.linalg.norm(T, axis=1, keepdims=True)


def key_prior(key):
    """Prior over the 25 states: diatonic triads 1.0, others KEY_PRIOR_OUT, N 1.0. key=None -> uniform."""
    w = np.ones(25)
    if key is None:
        return w / w.sum()
    k = note_pc(key)
    diatonic = {k, (k + 5) % 12, (k + 7) % 12, 12 + (k + 2) % 12, 12 + (k + 4) % 12, 12 + (k + 9) % 12}
    w[:24] = [1.0 if i in diatonic else P.KEY_PRIOR_OUT for i in range(24)]
    return w / w.sum()


# open guitar voicings (MIDI notes, low to high); others are built by `voicing`
_OPEN = {
    "C": [48, 52, 55, 60, 64], "G": [43, 47, 50, 55, 59, 67], "Am": [45, 52, 57, 60, 64],
    "F": [41, 48, 53, 57, 60, 65], "Em": [40, 47, 52, 55, 59, 64], "E": [40, 47, 52, 56, 59, 64],
    "Bb": [46, 53, 58, 62, 65], "D": [50, 57, 62, 66], "A": [45, 52, 57, 61, 64],
    "Dm": [50, 57, 62, 65], "Bm": [47, 54, 59, 62, 66], "Am/G": [43, 52, 57, 60, 64],
}


def voicing(label, capo=0):
    """Guitar voicing of a chord shape, transposed by capo. Unknown shapes: E-shape or A-shape barre."""
    if label in _OPEN:
        return [n + capo for n in _OPEN[label]]
    r, m = parse_chord(label)
    if r >= 5 or r == 4:  # E-shape barre on the low E string
        base = 40 + (r - 4) % 12
        shape = [0, 7, 12, 15 if m else 16, 19, 24]
    else:                 # A-shape barre on the A string
        base = 45 + (r - 9) % 12
        shape = [0, 7, 12, 15 if m else 16, 19]
    return [base + s + capo for s in shape]
