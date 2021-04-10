export function InvokeEvent(events: Set<Function>, name: string, args: any) {
    var event = new CustomEvent(name, { detail: args });
    Object.assign(event, args);
    for (const handler of events) {
        handler(event);
    }
}

export function InvokeRawEvent(events: Set<Function>, name: string, args: any) {
    for (const handler of events) {
        handler(args);
    }
}