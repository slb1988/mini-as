int main() {
    HostRef@ value = HostRef(40);
    value.value += 2;
    return value.value;
}
