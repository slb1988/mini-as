int main() {
    HostRef@ value = HostRef(42);
    return value is null ? 0 : 42;
}
