export function GenerateShim(name: string) {
    return function <T extends { new(...args: any[]): {} }>(constructor: T) {
        return class extends constructor {
            constructor(...args) {
                super(...args);
                return new Proxy(this, new ShimProxyHandler(name));
            }
        };
    }
}

const disallowedKeys = [
    "addEventListener",
    "removeEventListener",
    "then",
    "toString",
    Symbol.toStringTag,
    Symbol.toPrimitive
]

export class ShimProxyHandler<T extends Object> implements ProxyHandler<T> {
    name: string;
    constructor(name?: string) {
        this.name = name;
    }

    get(target: T, key: any, reciever?: any) {
        let f = Reflect.get(target, key, reciever);

        if (!disallowedKeys.includes(key) && !(String(key)[0] == '_') && !(key instanceof Symbol))
            (f === undefined ? console.error : f === null ? console.warn : console.info)(`get: ${this.name ?? target.constructor?.name}.${String(key)} -> ${(typeof f == 'function' ? f.name : f)}`);

        return f;
    }

    set(target: T, key: any, value: any, reciever?: any) {
        if (!disallowedKeys.includes(key) && !(String(key)[0] == '_'))
            console.info(`set: ${this.name ?? target.constructor?.name}.${String(key)} -> ${value}`);

        target[key] = value;
        return true;
    }
}

