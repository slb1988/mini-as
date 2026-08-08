class Counter {
    int value;
    void set(int next) { value = next; }
    int add(int delta) { value += delta; return value; }
    int addTwice(int delta) { return add(delta) + add(delta); }
}

int main() {
    Counter@ counter = Counter();
    counter.set(38);
    return counter.addTwice(2);
}
