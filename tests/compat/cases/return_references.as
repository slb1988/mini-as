int property = 1;

int &access() {
    return property;
}

int main() {
    access() = 40;
    access() += 2;
    return access();
}
