/// <reference lib="webworker" />
import kuromoji from '@sglkc/kuromoji';
import { segments, type JapaneseMode, type Token } from './japanese';

// Builds kuromoji once and answers { id, lines, mode } with one segment list per line. The IPADIC
// dictionary is served with the app from /kuromoji/ (vite.config.ts); the path is root-relative
// because kuromoji collapses the // of https://.
//
// WebView2 serves the app's virtual host to the page but not to workers, so kuromoji's fetch of a
// dictionary file is answered by the page: { file, url } out, { file, buffer | error } back.
const files = new Map<number, { resolve: (response: Response) => void; reject: (error: Error) => void }>();
let nextFile = 0;
self.fetch = ((input: RequestInfo | URL) => new Promise<Response>((resolve, reject) => {
  const file = ++nextFile;
  files.set(file, { resolve, reject });
  self.postMessage({ file, url: String(input) });
})) as typeof fetch;

type Tokenizer = { tokenize(text: string): Token[] };
let tokenizer: Promise<Tokenizer> | undefined;
const load = () => tokenizer ??= new Promise<Tokenizer>((resolve, reject) =>
  kuromoji.builder({ dicPath: '/kuromoji/' }).build((error: unknown, built: Tokenizer) => error ? reject(error) : resolve(built)));

type Message = { file: number; buffer?: ArrayBuffer; error?: string } | { file?: undefined; id: number; lines: string[]; mode: JapaneseMode };
self.onmessage = async (event: MessageEvent<Message>) => {
  const data = event.data;
  if (data.file !== undefined) {
    const request = files.get(data.file);
    files.delete(data.file);
    if (data.buffer) request?.resolve(new Response(data.buffer)); else request?.reject(new Error(data.error || '日语词典加载失败'));
    return;
  }
  const { id, lines, mode } = data;
  try {
    const built = await load();
    self.postMessage({ id, segments: lines.map(line => line ? segments(built.tokenize(line), mode) : []) });
  } catch (error) {
    tokenizer = undefined; // a failed dictionary load can be retried
    self.postMessage({ id, error: error instanceof Error ? error.message : String(error) });
  }
};
