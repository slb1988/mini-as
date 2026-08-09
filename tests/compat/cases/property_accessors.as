interface IValue {
    int amount { get const; set; }
}

class Meter : IValue {
    private int stored;

    Meter() { stored = 1; }

    int amount {
        get const { return stored; }
        set { stored = value * 2; }
    }

    int get_raw() const property { return stored; }
    void set_raw(int input) property { stored = input; }
    int bumpRaw() { raw += 1; return raw; }
}

Meter@ shared = Meter();
int receiverCalls = 0;

Meter@ select() {
    receiverCalls++;
    return shared;
}

int main() {
    IValue@ view = shared;
    int inner = shared.bumpRaw();
    view.amount = 10;
    view.amount += 1;
    select().raw += 3;
    return view.amount + shared.raw + receiverCalls + inner;
}
