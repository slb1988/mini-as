int main() {
    HostBox<int>@ integers;
    HostBox<float>@ decimals;
    HostBox<int>@ repeated;
    return integers is null && decimals is null && repeated is null ? 42 : 0;
}
