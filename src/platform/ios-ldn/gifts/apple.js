// AGPL-3.0-or-later. Apple host adapter for GB-Link's unmodified gift modules.
// Native callbacks: giftSend(number[]), giftStatus(string, detail), giftLog(string).
globalThis.Gifts = (() => {
    const { EVENTS, EVENT_GROUPS, findEvent } = modules['gift/events'];
    const { wc3Event } = modules['gift/wc3'];
    const { RfuLeader } = modules['cable/leader'];
    const { GiftSession } = modules['gift/session'];
    let leader = null, session = null, imported = null;
    function stop() {
        if (session) { session.running = false; session.stop(); }
        session = leader = null;
    }
    return {
        catalogue: () => EVENT_GROUPS.map(group => ({ label: group.label, events: group.events.map(
            e => ({ id: e.id, label: e.label, description: e.description, roms: e.roms ?? [] })) })),
        importCard(bytes, name) {
            imported = wc3Event(Uint8Array.from(bytes), name);
            return { id: imported.id, label: imported.label, description: imported.description };
        },
        start(id) {
            const event = id === 'wc3-file' ? imported : findEvent(id);
            if (!event) throw new Error('Choose a Wonder Card first.');
            stop();
            leader = new RfuLeader({ send: f => giftSend(Array.from(f)), log: giftLog });
            session = new GiftSession({ leader, log: giftLog });
            session.onStatus = s => giftStatus(s.stage, { detail: s.detail ?? null });
            session.onDecision = d => { if (d) giftStatus('decision', { reasons: d.reasons }); };
            session.onResult = r => giftStatus('result', { outcome: r.outcome, message: r.message ?? '' });
            session.setEvent(event); session.start();
        },
        tick() { leader?.tick(); },
        receive(type, header, bytes) { leader?.boardFrame({ type, header, frame: Uint8Array.from(bytes) }); },
        decide(send) { session?.decide(send); },
        stop,
    };
})();
