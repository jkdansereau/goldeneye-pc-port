// GoldenEye 007 Online -- live status page. A WebSocket (/api/live) streams
// snapshots of the directory; if that fails we poll /api/stats. All
// player-supplied text goes into the page via textContent only.
"use strict";

(() => {
  const $ = (id) => document.getElementById(id);
  let snap = null;
  let receivedAt = 0;
  let ws = null;
  let pollTimer = null;
  let retryMs = 1000;

  function el(tag, cls, text) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null) e.textContent = String(text);
    return e;
  }

  function setConn(kind, text) {
    const c = $("conn");
    c.className = "pill " + (kind === "live" ? "pill-live" : kind === "off" ? "pill-off" : "pill-wait");
    c.textContent = text;
  }

  function clock(sec) {
    sec = Math.max(0, Math.floor(sec));
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    return (h ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s).padStart(2, "0");
  }

  function ago(ms) {
    const s = Math.max(0, Math.round(ms / 1000));
    if (s < 60) return "just now";
    if (s < 3600) return Math.round(s / 60) + " min ago";
    if (s < 86400) return Math.round(s / 3600) + " h ago";
    return Math.round(s / 86400) + " d ago";
  }

  // Server time now, from the snapshot's clock plus local elapsed time.
  const serverNow = () => (snap ? snap.now + (Date.now() - receivedAt) : Date.now());

  function stateLabel(s) {
    if (s.state === "playing") return ["In match", "b-playing"];
    if (s.state === "starting") return ["Starting", "b-starting"];
    return ["Waiting " + s.numPlayers + "/" + s.maxPlayers, "b-waiting"];
  }

  function sessionCard(s) {
    const card = el("article", "card");
    const head = el("div", "card-head");
    const titles = el("div");
    titles.append(el("div", "card-title", s.private ? "Private game" : s.name || "Lobby"));
    if (!s.private) titles.append(el("div", "card-sub", "hosted by " + (s.host || "?") + (s.quick ? " · quick match" : "")));
    const [label, cls] = stateLabel(s);
    head.append(titles, el("span", "badge " + cls, label));
    card.append(head);

    const meta = el("div", "meta");
    const add = (k, v) => {
      const span = el("span");
      span.append(k + " ", el("b", null, v));
      meta.append(span);
    };
    add("Stage", s.stage);
    add("Mode", s.scenario);
    if (s.weapons && s.weapons !== "?") add("Weapons", s.weapons);
    if (s.length && s.length !== "?") add("Length", s.length);
    if (s.state === "playing") add("Time", clock((s.matchSec || 0) + (serverNow() - (snap ? snap.now : 0)) / 1000));
    else add("Open for", clock((serverNow() - s.since) / 1000));
    if (s.country) add("Region", s.country);
    card.append(meta);

    if (!s.private) {
      const list = el("ul", "players");
      for (const p of s.players || []) {
        const li = el("li");
        li.append(el("span", "who", p.name), el("span", "chr", p.character));
        list.append(li);
      }
      for (let i = (s.players || []).length; i < s.maxPlayers; i++) {
        const li = el("li");
        li.append(el("span", "open", "open slot"));
        list.append(li);
      }
      card.append(list);
      if (s.code) {
        const join = el("div", "card-sub");
        join.append("Join code ", el("span", "code", s.code));
        card.append(join);
      }
    } else {
      card.append(el("div", "card-sub", s.numPlayers + " of " + s.maxPlayers + " players"));
    }
    return card;
  }

  function recentItem(m) {
    const li = el("li");
    const when = el("div", "when");
    when.append(el("b", null, m.stage + " · " + m.scenario));
    when.append(ago(serverNow() - m.ended) + " · " + clock(m.duration));
    const scores = el("div", "scores");
    const best = Math.max(...m.players.map((p) => p.kills));
    for (const p of m.players.slice().sort((a, b) => b.kills - a.kills || a.deaths - b.deaths)) {
      const s = el("span", "score" + (p.kills === best && best > 0 ? " win" : ""));
      s.append(p.name + " (" + p.character + ")");
      s.append(el("span", "k", p.kills + "/" + p.deaths));
      scores.append(s);
    }
    li.append(when, scores);
    return li;
  }

  function render() {
    if (!snap) return;
    $("s-online").textContent = snap.online;
    $("s-lobbies").textContent = snap.lobbies;
    $("s-matches").textContent = snap.matches;
    $("s-today").textContent = snap.today ? snap.today.matches : 0;
    const notes = [];
    if (snap.searching) notes.push(snap.searching + " searching for a quick match");
    if (snap.today && snap.today.peak) notes.push("today's peak: " + snap.today.peak + " online");
    $("live-note").textContent = notes.join(" · ");

    const sessions = $("sessions");
    sessions.replaceChildren(...snap.sessions.map(sessionCard));
    $("sessions-empty").hidden = snap.sessions.length > 0;

    const recent = $("recent");
    recent.replaceChildren(...(snap.recent || []).map(recentItem));
    $("recent-empty").hidden = (snap.recent || []).length > 0;
  }

  function accept(data) {
    try {
      snap = JSON.parse(data);
      receivedAt = Date.now();
      render();
    } catch {
      /* ignore a bad frame */
    }
  }

  async function poll() {
    try {
      const r = await fetch("/api/stats", { cache: "no-store" });
      if (!r.ok) throw new Error(r.status);
      accept(await r.text());
      setConn("wait", "Updating every 30 s");
    } catch {
      setConn("off", "Offline");
    }
  }

  function startPolling() {
    if (pollTimer) return;
    poll();
    pollTimer = setInterval(() => {
      if (!document.hidden) poll();
    }, 30000);
  }

  function connect() {
    const proto = location.protocol === "https:" ? "wss:" : "ws:";
    try {
      ws = new WebSocket(proto + "//" + location.host + "/api/live");
    } catch {
      startPolling();
      return;
    }
    ws.onopen = () => {
      retryMs = 1000;
      setConn("live", "Live");
      if (pollTimer) {
        clearInterval(pollTimer);
        pollTimer = null;
      }
    };
    ws.onmessage = (ev) => accept(ev.data);
    ws.onclose = () => {
      ws = null;
      setConn("wait", "Reconnecting…");
      startPolling();
      setTimeout(connect, retryMs);
      retryMs = Math.min(retryMs * 2, 60000);
    };
  }

  // Clocks tick locally between snapshots.
  setInterval(() => {
    if (snap && !document.hidden) render();
  }, 1000);

  connect();
})();
