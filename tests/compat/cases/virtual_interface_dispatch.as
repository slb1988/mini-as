interface IValue {
    int get();
}

class First : IValue {
    int get() { return 40; }
}

class Second : IValue {
    int get() { return 2; }
}

int main() {
    IValue@ first = First();
    IValue@ second = Second();
    return first.get() + second.get();
}
