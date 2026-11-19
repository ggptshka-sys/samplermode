from pathlib import Path
import math
import random
import struct
import wave

ROOT = Path(__file__).resolve().parents[1] / "example-library" / "Samples"
ROOT.mkdir(parents=True, exist_ok=True)
RATE = 44100
random.seed(42)


def save(name, duration, tone=None, noise=False, decay=1.0):
    frames = bytearray()
    for index in range(int(RATE * duration)):
        time = index / RATE
        envelope = max(0.0, 1.0 - time / duration) ** decay
        attack = min(1.0, index / 64.0)
        source = math.sin(2.0 * math.pi * tone * time) if tone else random.uniform(-1.0, 1.0)
        if noise:
            source *= 0.6 if index % 3 else 1.0
        value = int(max(-1.0, min(1.0, source * envelope * attack)) * 24000)
        frames.extend(struct.pack("<h", value))
    with wave.open(str(ROOT / name), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes(frames)


save("060_C_down_1.wav", 1.2, tone=261.63, decay=1.8)
save("060_C_down_2.wav", 0.25, tone=523.25, decay=2.5)
save("061_closed_hat.wav", 0.12, noise=True, decay=3.0)
save("062_open_hat.wav", 1.4, noise=True, decay=1.2)
