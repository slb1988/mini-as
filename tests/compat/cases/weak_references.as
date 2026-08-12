class Payload {
    int value;

    Payload(int initial) {
        value = initial;
    }
}

int main() {
    Payload@ object = Payload(42);
    weakref<Payload> reference(object);
    if (reference != object) return -2;
    Payload@ alive = reference.get();
    int result = alive is null ? 0 : alive.value;
    @alive = null;
    @object = null;
    return reference.get() is null ? result : 0;
}
