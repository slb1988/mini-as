int constructions = 0;

class Payload {
    int value;
}

class Base {
    int baseValue;
    Base() {
        constructions += 1;
    }
}

class Derived : Base {
    int ownValue;
    Payload@ payload;
}

int main() {
    Derived@ original = Derived();
    original.baseValue = 3;
    original.ownValue = 4;
    @original.payload = Payload();
    original.payload.value = 5;

    Derived@ copied = Derived(original);
    original.baseValue = 30;
    original.ownValue = 40;
    original.payload.value = 50;
    return copied.baseValue * 100 + copied.ownValue * 10 + copied.payload.value + constructions;
}
