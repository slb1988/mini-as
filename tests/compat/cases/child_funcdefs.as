class Dispatcher {
    funcdef int Callback(int value);
    Callback@ callback;

    int invoke(int value) {
        return callback(value);
    }
}

class DerivedDispatcher : Dispatcher {
    Callback@ derivedCallback;

    int invokeDerived(int value) {
        return derivedCallback(value);
    }
}

int twice(int value) { return value * 2; }

int main() {
    DerivedDispatcher@ box = DerivedDispatcher();
    Dispatcher::Callback@ callback = @twice;
    @box.derivedCallback = @callback;
    return box.invokeDerived(21);
}
