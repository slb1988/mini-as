HostColor selected = HostRed;
HostTransform@ transform = @Lift;

int main() {
    return transform(40) + (selected == HostRed ? 0 : 100);
}
