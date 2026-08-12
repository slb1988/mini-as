external shared interface ICounter;
external shared class Counter;
external shared int Twice(int value);

int main() {
    Counter@ counter = Counter(40);
    ICounter@ view = counter;
    return view.read() + Twice(1);
}
