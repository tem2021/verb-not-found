import { FormEvent, KeyboardEvent, useEffect, useRef, useState } from 'react';

type Cwrap = (name: string, result: string | null, args: string[]) => (...values: unknown[]) => number | void;
type WasmModule = { cwrap: Cwrap };
type Page = { text: string; clearBefore: boolean; prompt: string | null };

declare global {
  interface Window {
    createVerbNotFoundModule?: (options: Record<string, unknown>) => Promise<WasmModule>;
  }
}

const BOOT_SCREEN = `VERB NOT FOUND

An Interactive Fiction for EPC Class 5
Release 2 / Serial number 260828

[ loading the archived parser... ]`;

function splitPages(raw: string): Page[] {
  const pages: Page[] = [];
  const marker = /@@VNF_(CLEAR|PAUSE:([^@]*))@@\n?/g;
  let cursor = 0;
  let text = '';
  let clearBefore = false;
  let match: RegExpExecArray | null;

  while ((match = marker.exec(raw)) !== null) {
    text += raw.slice(cursor, match.index);
    if (match[1] === 'CLEAR') {
      if (text.trim()) {
        pages.push({ text, clearBefore, prompt: null });
        text = '';
      }
      clearBefore = true;
    } else {
      pages.push({ text, clearBefore, prompt: match[2] || 'continue' });
      text = '';
      clearBefore = false;
    }
    cursor = marker.lastIndex;
  }
  text += raw.slice(cursor);
  if (text.trim()) pages.push({ text, clearBefore, prompt: null });
  return pages;
}

function loadRuntime(url: string): Promise<void> {
  if (window.createVerbNotFoundModule) return Promise.resolve();
  return new Promise((resolve, reject) => {
    const script = document.createElement('script');
    script.src = url;
    script.onload = () => resolve();
    script.onerror = () => reject(new Error('WebAssembly runtime not found'));
    document.head.appendChild(script);
  });
}

export default function Terminal() {
  const [screen, setScreen] = useState(BOOT_SCREEN);
  const [command, setCommand] = useState('');
  const [ready, setReady] = useState(false);
  const [waiting, setWaiting] = useState<string | null>(null);
  const [ended, setEnded] = useState(false);
  const [panel, setPanel] = useState<'guide' | 'about' | null>(null);
  const [historyPosition, setHistoryPosition] = useState(0);
  const capture = useRef<string[]>([]);
  const transcript = useRef('');
  const pages = useRef<Page[]>([]);
  const commands = useRef<string[]>([]);
  const booted = useRef(false);
  const initGame = useRef<(() => void) | null>(null);
  const sendCommand = useRef<((line: string) => number) | null>(null);
  const screenNode = useRef<HTMLPreElement>(null);
  const inputNode = useRef<HTMLInputElement>(null);

  const presentNext = () => {
    const page = pages.current.shift();
    if (!page) {
      setWaiting(null);
      inputNode.current?.focus();
      return;
    }
    const prompt = page.prompt ? `\n[Enter] ${page.prompt}` : '';
    setScreen((previous) => {
      if (page.clearBefore) return `${page.text.trimStart()}${prompt}`;
      const spacer = previous.endsWith('\n') || !previous ? '' : '\n';
      return `${previous}${spacer}${page.text}${prompt}`;
    });
    setWaiting(page.prompt);
    window.requestAnimationFrame(() => inputNode.current?.focus());
  };

  const consumeOutput = () => {
    const output = capture.current.join('');
    capture.current = [];
    transcript.current += output
      .replace(/@@VNF_CLEAR@@\n?/g, '\n--- screen cleared ---\n')
      .replace(/@@VNF_PAUSE:([^@]*)@@\n?/g, '\n[Enter] $1\n');
    pages.current.push(...splitPages(output));
    presentNext();
  };

  const startFresh = () => {
    capture.current = [];
    pages.current = [];
    commands.current = [];
    transcript.current = '';
    setHistoryPosition(0);
    setEnded(false);
    setScreen('');
    initGame.current?.();
    consumeOutput();
  };

  useEffect(() => {
    if (booted.current) return;
    booted.current = true;
    const base = new URL(`${import.meta.env.BASE_URL}game/`, window.location.href);

    loadRuntime(new URL('verb-not-found.js', base).href)
      .then(async () => {
        if (!window.createVerbNotFoundModule) throw new Error('Runtime factory unavailable');
        const module = await window.createVerbNotFoundModule({
          locateFile: (file: string) => new URL(file, base).href,
          print: (line: string) => capture.current.push(`${line}\n`),
          printErr: (line: string) => capture.current.push(`[runtime] ${line}\n`),
        });
        initGame.current = module.cwrap('web_game_init', null, []) as () => void;
        sendCommand.current = module.cwrap('web_game_command', 'number', ['string']) as (line: string) => number;
        setReady(true);
        startFresh();
      })
      .catch(() => {
        setScreen(`${BOOT_SCREEN}\n\nThe WebAssembly build is unavailable. Run MAKE WEB, then reload.`);
      });
  }, []);

  useEffect(() => {
    const node = screenNode.current;
    if (node) node.scrollTop = node.scrollHeight;
  }, [screen]);

  useEffect(() => {
    const handleFunctionKeys = (event: globalThis.KeyboardEvent) => {
      if (event.key === 'F1') {
        event.preventDefault();
        setPanel('guide');
      } else if (event.key === 'F2') {
        event.preventDefault();
        setPanel('about');
      } else if (event.key === 'Escape') {
        setPanel(null);
      }
    };
    window.addEventListener('keydown', handleFunctionKeys);
    return () => window.removeEventListener('keydown', handleFunctionKeys);
  }, []);

  const advance = () => {
    setWaiting(null);
    presentNext();
  };

  const submit = (event: FormEvent) => {
    event.preventDefault();
    if (waiting) {
      advance();
      return;
    }
    const line = command.trim();
    if (!ready || ended || !line || !sendCommand.current) return;
    setScreen((previous) => `${previous}${previous.endsWith('\n') ? '' : '\n'}\n> ${line}\n`);
    transcript.current += `\n> ${line}\n`;
    setCommand('');
    commands.current.push(line);
    setHistoryPosition(commands.current.length);
    capture.current = [];
    const status = sendCommand.current(line);
    consumeOutput();
    if (status !== 0) setEnded(true);
  };

  const browseHistory = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
    event.preventDefault();
    const next = event.key === 'ArrowUp'
      ? Math.max(0, historyPosition - 1)
      : Math.min(commands.current.length, historyPosition + 1);
    setHistoryPosition(next);
    setCommand(next === commands.current.length ? '' : commands.current[next]);
  };

  const restart = () => {
    if (commands.current.length && !window.confirm('Begin the story again from the first room?')) return;
    startFresh();
  };

  const copyTranscript = async () => {
    await navigator.clipboard.writeText(transcript.current || screen);
  };

  return (
    <main className="archive-shell">
      <header className="archive-header">
        <p><span>archive@epc</span>:~/games$ ./verb-not-found</p>
        <p className="release">tty/web · release 2 · 260828</p>
      </header>

      <section
        className="terminal-frame"
        aria-label="Interactive fiction terminal"
        onClick={() => inputNode.current?.focus()}
      >
        <div className="terminal-bar" aria-hidden="true">
          <span>VERB NOT FOUND</span>
          <span>{ready ? 'WASM · CONNECTED' : 'CONNECTING...'}</span>
        </div>
        <pre ref={screenNode} className="terminal-screen" aria-live="polite" aria-atomic="false">{screen}</pre>
        <form className="command-line" onSubmit={submit}>
          <label htmlFor="command">&gt;</label>
          <input
            ref={inputNode}
            id="command"
            name="command"
            value={command}
            onChange={(event) => setCommand(event.target.value)}
            onKeyDown={browseHistory}
            autoComplete="off"
            autoCapitalize="none"
            spellCheck={false}
            disabled={!ready || (ended && !waiting)}
            readOnly={Boolean(waiting)}
            aria-label="Game command"
            placeholder={waiting ? 'Press Enter to continue' : ended ? 'The story has ended' : ''}
          />
          <button type="submit" disabled={!ready || (ended && !waiting)}>
            {waiting ? '[CONTINUE]' : '[RETURN]'}
          </button>
        </form>
      </section>

      <footer className="archive-footer">
        <p>{ready ? '[READY]' : '[BOOT]'} Type short English commands. Begin with <strong>L</strong>.</p>
        <nav aria-label="Game controls">
          <button type="button" onClick={() => setPanel('guide')}>F1 GUIDE</button>
          <button type="button" onClick={() => setPanel('about')}>F2 ABOUT</button>
          <button type="button" onClick={copyTranscript}>COPY LOG</button>
          <button type="button" onClick={restart} disabled={!ready}>RESTART</button>
        </nav>
      </footer>

      {panel && (
        <div className="panel-backdrop" role="presentation" onMouseDown={() => setPanel(null)}>
          <section className="info-panel" role="dialog" aria-modal="true" aria-labelledby="panel-title" onMouseDown={(event) => event.stopPropagation()}>
            <button className="panel-close" type="button" onClick={() => setPanel(null)} aria-label="Close">[X]</button>
            {panel === 'guide' ? (
              <>
                <p className="eyebrow">-- FIELD MANUAL --</p>
                <h2 id="panel-title">COMMAND GUIDE</h2>
                <pre>{`L / LOOK          describe the room
X THING           examine something
I / INVENTORY     check what you carry
N S E W           move by direction
READ MESSAGES     recover verbs
WORDS             quick vocabulary
HELP              parser instructions
HINT              progressive local help
G / AGAIN         repeat a command`}</pre>
                <p>Capitalization does not matter. Words printed in CAPITALS are usually worth examining.</p>
              </>
            ) : (
              <>
                <p className="eyebrow">-- ARCHIVE NOTE --</p>
                <h2 id="panel-title">ABOUT THIS GAME</h2>
                <p>Created for the final morning of EPC Class 5 at CUHK-Shenzhen, August 2026.</p>
                <p>The original parser is written in C. This page runs that same game logic locally in your browser through WebAssembly. No command or message is sent to a server.</p>
                <p>Public messages follow the choices made in the questionnaire. Private messages remain sealed.</p>
              </>
            )}
          </section>
        </div>
      )}
    </main>
  );
}
