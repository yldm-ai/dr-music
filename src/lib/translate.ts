// Lyric translation for 「译」: either 小牛翻译 (NiuTrans, online, the user's own App ID / API Key)
// or Tencent's Hunyuan model (Hy-MT2 1.8B) in a local Ollama. All requests are made by the C++
// host (native/niutrans.cpp, native/local_translator.cpp); this module picks languages and calls
// the fixed bridge methods.
export type TranslateTarget = 'zh' | 'en';
export type TranslateProvider = 'local' | 'niutrans';
export interface LocalStatus { reachable: boolean; installed: boolean; version: string; model: string; endpoint: string; error: string; size?: number }
export interface TranslatorStatus { provider: TranslateProvider; local: LocalStatus | null; niutrans: { configured: boolean; appId: string }; systemProxy?: string }
/** PowerShell that makes the Ollama service use a proxy (it ignores the Windows proxy setting); Ollama must be restarted after. */
export const ollamaProxyCommand = (proxy: string) => `[Environment]::SetEnvironmentVariable('HTTPS_PROXY', '${proxy}', 'User')`;
/** Whether an Ollama pull error looks like the registry could not be reached (blocked / needs a proxy). */
export const unreachableRegistry = (message: string) => /EOF|hf\.co|huggingface|registry|connection|timeout|tls|handshake|dial tcp/i.test(message);
export interface PullProgress { status: string; completed: number; total: number }
export const PROVIDERS: { value: TranslateProvider; label: string }[] = [
  { value: 'niutrans', label: '小牛翻译（在线）' },
  { value: 'local', label: '混元 Hy-MT2（本地 Ollama）' },
];
export const OLLAMA_INSTALL = 'irm https://ollama.com/install.ps1 | iex';

/** The lyric's main language, from its script: enough to pick the prompt and skip same-language lyrics. */
export function sourceLanguage(text: string): 'zh' | 'ja' | 'ko' | 'en' {
  if (/[\p{Script=Hiragana}\p{Script=Katakana}]/u.test(text)) return 'ja';
  if (/\p{Script=Hangul}/u.test(text)) return 'ko';
  if (/\p{Script=Han}/u.test(text)) return 'zh';
  return 'en';
}

export type Readiness = 'checking' | 'same' | 'no-key' | 'no-ollama' | 'no-model' | 'ready';
/** Whether translating a lyric in `source` to `target` works with the chosen provider as it is now. */
export function translateState(status: TranslatorStatus | undefined, source: string, target: TranslateTarget): Readiness {
  if (source === target) return 'same';
  if (!status) return 'checking';
  if (status.provider === 'niutrans') return status.niutrans.configured ? 'ready' : 'no-key';
  if (!status.local?.reachable) return 'no-ollama';
  return status.local.installed ? 'ready' : 'no-model';
}
export const READINESS_LABEL: Record<Readiness, string> = { same: '已是原文', checking: '检查中', 'no-key': '未填写密钥', 'no-ollama': '未运行 Ollama', 'no-model': '未下载模型', ready: '' };

export const translatorStatus = (local = false) => window.music.translateStatus(local);
export const downloadModel = () => window.music.translatePull();

/** One translation per line ("" for blank lines); results for a superseded request are dropped by the caller. */
export async function translateLines(lines: string[], source: string, target: TranslateTarget, signal: AbortSignal): Promise<string[]> {
  const result = await window.music.translateLines(lines, source, target);
  if (signal.aborted) throw new DOMException('aborted', 'AbortError');
  return result;
}
