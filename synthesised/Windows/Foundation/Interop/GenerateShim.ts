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

export class ShimProxyHandler<T extends Object> implements ProxyHandler<T> {
    name: string;
    constructor(name?: string) {
        this.name = name;
    }

    get(target: T, key: any) {
        let f = target[key];

        if (key === "addEventListener" || key === "targetElement")
            return f;

        (f === undefined ? console.error : console.warn)(`shim: ${this.name ?? target.constructor?.name}.${key}`);

        return f;
    }
}

