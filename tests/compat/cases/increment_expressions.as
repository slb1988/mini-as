int global = 1;

int main() {
    int local = 1;
    int prefix = ++local;
    int postfix = local++;
    int globalNew = ++global;
    return local * 1000 + prefix * 100 + postfix * 10 + globalNew;
}
