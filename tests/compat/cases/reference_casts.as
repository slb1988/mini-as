interface IValue {
    int score();
}

class Base {
    int score() { return 1; }
}

class Derived : Base, IValue {
    int score() { return 42; }
}

int main() {
    Derived@ original = Derived();
    Base@ good = original;
    Base@ bad = Base();
    IValue@ iface = original;
    Derived@ fromBase = cast<Derived>(good);
    Derived@ failed = cast<Derived>(bad);
    Derived@ fromInterface = cast<Derived>(iface);
    return (fromBase is original ? fromBase.score() : 0) +
           fromInterface.score() + (failed is null ? 0 : 100);
}
