import { useEffect, useState } from 'react';
import { Select } from './Select';
import { DEFAULT_MODEL, modelSize, PROVIDERS, providerOf, translatorStatus, type TranslateProvider, type TranslatorStatus } from './lib/translate';
import type { Settings } from './types';

// Settings → 工具 → 歌词翻译: the provider for 「译」 and its setup. 哔哩哔哩 Index-Translate is free and
// needs no key, only a choice of model; 小牛翻译 needs the user's own App ID and API Key (stored
// DPAPI-encrypted by the host, never shown again).
const MODEL_NOTES: Record<string, string> = { '2B': '速度最快', '9B': '速度与质量均衡', '35B-A3B': '质量最好，稍慢' };
const message = (error: unknown) => error instanceof Error ? error.message : String(error);

export function TranslationSettings({ settings, setSettings, notify }: { settings?: Settings; setSettings: (patch: Partial<Settings>) => void; notify: (message: string) => void }) {
  const provider = providerOf(settings?.translateProvider);
  const [status, setStatus] = useState<TranslatorStatus>();
  const [testing, setTesting] = useState(false);
  const [appId, setAppId] = useState('');
  const [apikey, setApikey] = useState('');
  const refresh = () => { void translatorStatus().then(setStatus, () => setStatus(undefined)); };
  useEffect(refresh, []);
  useEffect(() => window.music.onTranslateChanged(refresh), []); // after the host has applied a new provider / model
  useEffect(() => { if (status?.niutrans.appId && !appId) setAppId(status.niutrans.appId); }, [status?.niutrans.appId]);
  const models = (status?.bilibili.models ?? [DEFAULT_MODEL]).map(value => {
    const size = modelSize(value), note = MODEL_NOTES[size];
    return { value, label: note ? `${size} · ${note}` : size };
  });
  const saved = settings?.translateModel;
  const model = saved && models.some(option => option.value === saved) ? saved : status?.bilibili.model ?? DEFAULT_MODEL;

  const test = () => {
    setTesting(true);
    window.music.translateTest().then(lines => notify(`测试成功：${lines.join(' / ')}`), error => notify(`测试失败：${message(error)}`)).finally(() => setTesting(false));
  };
  const saveKey = (clear = false) => {
    window.music.setNiutransKey(clear ? '' : appId.trim(), clear ? '' : apikey.trim())
      .then(next => { setStatus(s => s && { ...s, niutrans: next }); setApikey(''); if (clear) setAppId(''); notify(clear ? '已清除小牛翻译密钥' : '已加密保存小牛翻译密钥'); }, error => notify(message(error)));
  };

  return <div className="translation-models" aria-label="歌词翻译">
    <div className="settings-row"><span className="settings-row-label"><span>翻译服务</span><small>播放页右上角「译」用它把歌词逐行译成中文或英文，译文保存在本机，同一句不会重复翻译。</small></span>
      <Select label="翻译服务" value={provider} options={PROVIDERS} onChange={value => setSettings({ translateProvider: value as TranslateProvider })}/></div>

    {provider === 'niutrans' ? <>
      <p className="settings-hint">在 niutrans.com 注册后进入「控制台 → API 应用」，开通「文本 API」即可得到 App ID 和 API Key。歌词会发送到小牛翻译的服务器（api.niutrans.com）进行翻译。</p>
      <div className="settings-field translation-key">
        <label><span>App ID</span><input aria-label="小牛翻译 App ID" value={appId} spellCheck={false} autoComplete="off" onChange={event => setAppId(event.target.value)}/></label>
        <label><span>API Key</span><input aria-label="小牛翻译 API Key" type="password" value={apikey} spellCheck={false} autoComplete="off" placeholder={status?.niutrans.configured ? '已保存，留空则不修改' : ''} onChange={event => setApikey(event.target.value)}/></label>
        <button type="button" className="secondary-button" disabled={!appId.trim() || !apikey.trim()} onClick={() => saveKey()}>保存</button>
      </div>
      <div className="translation-model-row">
        <span data-state={status?.niutrans.configured ? 'ready' : 'missing'}>{status?.niutrans.configured ? `已配置 · App ID ${status.niutrans.appId}` : '尚未填写 App ID 和 API Key'}</span>
        <button type="button" className="secondary-button" disabled={!status?.niutrans.configured} onClick={() => saveKey(true)}>清除</button>
        <button type="button" className="primary-button" disabled={!status?.niutrans.configured || testing} onClick={test}>{testing ? '测试中…' : '测试翻译'}</button>
      </div>
    </> : <>
      <p className="settings-hint">哔哩哔哩开源的 Index-Translate 翻译模型，免费使用，无需注册或密钥。歌词会发送到哔哩哔哩的服务器（index-translate.bilibili.com）进行翻译。</p>
      <div className="settings-row"><span className="settings-row-label"><span>模型</span><small>模型越大译文越好，但翻译得越慢。换模型后会重新翻译。</small></span>
        <Select label="翻译模型" value={model} options={models} onChange={value => setSettings({ translateModel: value })}/></div>
      <div className="translation-model-row">
        <span data-state="ready">{`可以使用 · Index-Translate-${modelSize(model)}`}</span>
        <button type="button" className="secondary-button" onClick={() => void window.music.openPage('index-translate').catch(error => notify(message(error)))}>项目主页</button>
        <button type="button" className="primary-button" disabled={testing} onClick={test}>{testing ? '测试中…' : '测试翻译'}</button>
      </div>
    </>}
  </div>;
}
