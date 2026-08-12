shared interface ICounter { int read(); }
shared class Counter : ICounter {
    int value;
    Counter(int start) { value = start; }
    int read() { return value; }
}
shared int Twice(int value) { return value * 2; }
import Counter@ Make() from "shared-source";

int main() {
    Counter@ value = Make();
    ICounter@ view = value;
    return view.read() + Twice(1);
}
