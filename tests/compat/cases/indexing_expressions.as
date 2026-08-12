int calls = 0;
int next() { calls += 1; return 0; }

int main() {
    array<int>@ values = {10, 20};
    int old = values[next()]++;
    values[next()] += 9;
    values[1] = 10;
    return old + values[0] + values[1] + calls;
}
