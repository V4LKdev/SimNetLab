- Single thread with rendering for simplicity of sharing data.
- Using uint64_t nanoseconds instead of doubles for slightly higher precision, but most crucially for determinism, as floating-point accumulation is order depenent and can vary from compiler and CPU.
[[Reference List#Fixed TPS simulation]]
