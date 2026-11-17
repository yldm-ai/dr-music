// Lyric translation for 「译」: 哔哩哔哩 Index-Translate (the default; a free public API, no key, the
// user picks one of its models) or 小牛翻译 (NiuTrans, the user's own App ID / API Key). All requests
// are made by the C++ host (native/index_translate.cpp, native/niutrans.cpp); this module picks
// languages and calls the fixed bridge methods.
export type TranslateTarget = 'zh' | 'en';
export type TranslateProvider = 'bilibili' | 'niutrans';
export interface TranslatorStatus {
  provider: TranslateProvider;
  bilibili: { model: string; models: string[] };
  niutrans: { configured: boolean; appId: string };
}
export const PROVIDERS: { value: TranslateProvider; label: string }[] = [
  { value: 'bilibili', label: '哔哩哔哩 Index-Translate' },
  { value: 'niutrans', label: '小牛翻译' },
];
export const DEFAULT_MODEL = 'Index-Translate-2B';
/** Index-Translate-2B → 2B, Index-Translate-35B-A3B → 35B-A3B. */
export const modelSize = (model: string) => model.replace(/^Index-Translate-/, '');
/** The chosen provider as stored (older libraries said 'local' for the removed Ollama model). */
export const providerOf = (value?: string): TranslateProvider => value === 'niutrans' ? 'niutrans' : 'bilibili';

/** The lyric's main language, from its script: enough to pick the prompt and skip same-language lyrics. */
export function sourceLanguage(text: string): 'zh' | 'ja' | 'ko' | 'en' {
  if (/[\p{Script=Hiragana}\p{Script=Katakana}]/u.test(text)) return 'ja';
  if (/\p{Script=Hangul}/u.test(text)) return 'ko';
  if (/\p{Script=Han}/u.test(text)) return 'zh';
  return 'en';
}

export type Readiness = 'checking' | 'same' | 'no-key' | 'ready';
/** Whether translating a lyric in `source` to `target` works with the chosen provider as it is now. */
export function translateState(status: TranslatorStatus | undefined, source: string, target: TranslateTarget): Readiness {
  if (source === target) return 'same';
  if (!status) return 'checking';
  if (status.provider === 'niutrans') return status.niutrans.configured ? 'ready' : 'no-key';
  return 'ready';
}
export const READINESS_LABEL: Record<Readiness, string> = { same: '已是原文', checking: '检查中', 'no-key': '未填写密钥', ready: '' };

/** Whether the lyric came with its own translation (TTML, LRC pairs, NetEase, QQ Music, Kugou). */
export const hasSourceTranslation = (lines: { translatedLyric?: string }[]) => lines.some(line => line.translatedLyric?.trim());

export const translatorStatus = () => window.music.translateStatus();

/** One translation per line ("" for blank lines); results for a superseded request are dropped by the caller. */
export async function translateLines(lines: string[], source: string, target: TranslateTarget, signal: AbortSignal): Promise<string[]> {
  const result = await window.music.translateLines(lines, source, target);
  if (signal.aborted) throw new DOMException('aborted', 'AbortError');
  return result;
}
