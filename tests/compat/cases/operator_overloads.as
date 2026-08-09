class Label {
    int value;
    Label(int input) { value = input; }
    int read() { return value; }
}

class CompareOnly {
    int value;
    CompareOnly(int input) { value = input; }
    int opCmp(CompareOnly@ other) { return value - other.value; }
}

class Number {
    int value;
    Number(int input) { value = input; }
    Number@ opAdd(Number@ other) { return Number(value + other.value); }
    int opAdd_r(int left) { return left + value; }
    int opNeg() { return -value; }
    int opCom() { return ~value; }
    bool opEquals(Number@ other) { return value == other.value; }
    int opCmp(Number@ other) { return value - other.value; }
    int opCmp(int other) { return value - other; }
    int opAddAssign(int delta) { value += delta; return value; }
    int opPreInc() { value++; return value; }
    int opPostInc() { int before = value; value++; return before; }
    int opCall(int scale) { return value * scale; }
    Label@ opCast() { return Label(value); }
    Label@ opImplCast() { return Label(value + 2); }
    int opConv() { return value; }
    int opImplConv() { return value + 1; }
}

int main() {
    Number@ first = Number(20);
    Number@ second = Number(22);
    Number@ sum = first + second;
    int reverse = 20 + second;
    int assigned = (first += 2);
    int prefix = ++first;
    int postfix = first++;
    Label@ label = cast<Label>(first);
    Label@ implicitLabel = first;
    int converted = int(first);
    int implicitValue = first;
    Number@ twin = Number(24);
    bool equalByValue = first == twin;
    bool sameHandle = first is twin;
    CompareOnly@ equalLeft = CompareOnly(7);
    CompareOnly@ equalRight = CompareOnly(7);
    return sum.value + reverse + (-second) + (~Number(0)) +
           (first == second ? 1 : 0) + (first >= second ? 1 : 0) +
           (20 < second ? 1 : 0) +
           assigned + prefix + postfix + first(1) + label.read() + converted + implicitValue +
           implicitLabel.read() + (equalByValue && !sameHandle ? 10 : 0) +
           (equalLeft == equalRight && !(equalLeft != equalRight) ? 10 : 0);
}
