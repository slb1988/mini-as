class NoDefault {
    NoDefault() delete;
    NoDefault(int value) {}
}

class NoCopy {
    NoCopy() {}
    NoCopy(const NoCopy &in other) delete;
}

class NoAssign {
    NoAssign &opAssign(const NoAssign &in other) delete;
}

int main() {
    NoDefault@ configured = NoDefault(1);
    NoCopy@ copyProtected = NoCopy();
    NoAssign@ left = NoAssign();
    NoAssign@ right = NoAssign();
    @left = right;
    return configured is null || copyProtected is null || !(left is right) ? 0 : 42;
}
