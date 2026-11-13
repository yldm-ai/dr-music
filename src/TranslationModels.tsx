import { useEffect, useState } from 'react';
import { Check, Copy } from 'lucide-react';
import { Select } from './Select';
import { downloadModel, OLLAMA_INSTALL, ollamaProxyCommand, PROVIDERS, translatorStatus, unreachableRegistry, type PullProgress, type TranslateProvider, type TranslatorStatus } from './lib/translate';
import type { Settings } from './types';

// Settings → 工具 → 歌词翻译: the provider for 「译」 and its setup. 小牛翻译 needs the user's own App ID
// and API Key (stored DPAPI-encrypted by the host, never shown again); the local Hunyuan model needs
// Ollama, which can be installed with one PowerShell command, and a one-time model download.
const DEFAULT_ENDPOINT = 'http://127.0.0.1:11434';
const DEFAULT_MODEL = 'hf.co/tencent/Hy-MT2-1.8B-GGUF:Q4_K_M';
const percent = (p: PullProgress) => p.total > 0 ? Math.min(100, Math.round(p.completed / p.total * 100)) : undefined;
const gigabytes = (bytes?: number) => bytes ? ` · ${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB` : '';
const message = (error: unknown) => error instanceof Error ? error.message : String(error);

export function TranslationSettings({ settings, setSettings, notify }: { settings?: Settings; setSettings: (patch: Partial<Settings>) => void; notify: (message: string) => void }) {
  const provider: TranslateProvider = settings?.translateProvider === 'niutrans' ? 'niutrans' : 'local';
  const [status, setStatus] = useState<TranslatorStatus>();
  const [checking, setChecking] = useState(false);
  const [testing, setTesting] = useState(false);
  const [progress, setProgress] = useState<PullProgress>();
  const [copied, setCopied] = useState('');
  const [pullFailed, setPullFailed] = useState('');
  const [endpoint, setEndpoint] = useState(settings?.translateEndpoint || DEFAULT_ENDPOINT);
  const [model, setModel] = useState(settings?.translateModel || DEFAULT_MODEL);
  const [appId, setAppId] = useState('');
  const [apikey, setApikey] = useState('');
  const refresh = () => { setChecking(true); void translatorStatus(true).then(setStatus, () => setStatus(undefined)).finally(() => setChecking(false)); };
  useEffect(refresh, []);
  useEffect(() => window.music.onTranslateChanged(refresh), []); // after the host has applied a new provider / address / model
  useEffect(() => window.music.onTranslateProgress(setProgress), []);
  useEffect(() => { if (status?.niutrans.appId && !appId) setAppId(status.niutrans.appId); }, [status?.niutrans.appId]);
  const local = status?.local, pulling = progress !== undefined, value = progress && percent(progress);

  const test = () => {
    setTesting(true);
    window.music.translateTest().then(lines => notify(`测试成功：${lines.join(' / ')}`), error => notify(`测试失败：${message(error)}`)).finally(() => setTesting(false));
  };
  const saveKey = (clear = false) => {
    window.music.setNiutransKey(clear ? '' : appId.trim(), clear ? '' : apikey.trim())
      .then(next => { setStatus(s => s && { ...s, niutrans: next }); setApikey(''); if (clear) setAppId(''); notify(clear ? '已清除小牛翻译密钥' : '已加密保存小牛翻译密钥'); }, error => notify(message(error)));
  };
  const download = () => {
    setProgress({ status: '正在连接 Ollama', completed: 0, total: 0 });
    setPullFailed('');
    downloadModel().then(next => notify(next.installed ? '翻译模型已下载' : '下载结束，但 Ollama 中没有找到该模型'), error => { notify(`下载失败：${message(error)}`); setPullFailed(message(error)); })
      .finally(() => { setProgress(undefined); refresh(); });
  };
  const importModel = () => {
    window.music.translateImport().then(result => {
      if (!result) return;
      setSettings({ translateModel: result.model }); setModel(result.model);
      notify('模型已导入 Ollama');
    }, error => notify(`导入失败：${message(error)}`)).finally(() => setProgress(undefined));
    setProgress({ status: '等待选择文件', completed: 0, total: 0 });
  };
  const saveLocal = () => {
    const url = endpoint.trim().replace(/\/+$/, ''), name = model.trim();
    if (!/^https?:\/\/(127\.0\.0\.1|localhost)(:\d{1,5})?$/.test(url)) { notify('只能使用本机地址，例如 http://127.0.0.1:11434'); return; }
    if (!/^[\w.:/-]{1,200}$/.test(name)) { notify('模型名称无效'); return; }
    setSettings({ translateEndpoint: url, translateModel: name });
  };
  const copy = (text: string) => void navigator.clipboard.writeText(text).then(() => { setCopied(text); setTimeout(() => setCopied(''), 1600); }, () => notify('复制失败，请手动选择命令'));
  const proxyCommand = status?.systemProxy ? ollamaProxyCommand(status.systemProxy) : '';

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
      <p className="settings-hint">腾讯混元翻译模型 Hy-MT2 1.8B（约 1.1 GB），由本机的 Ollama 运行，歌词不会离开这台电脑。</p>
      {!local?.reachable && <div className="install-command">
        <span>还没有 Ollama？在 PowerShell 中运行下面的命令安装，装好后点「重新检测」：</span>
        <div><code>{OLLAMA_INSTALL}</code><button type="button" className="secondary-button" aria-label="复制安装命令" onClick={() => copy(OLLAMA_INSTALL)}>{copied === OLLAMA_INSTALL ? <Check size={14}/> : <Copy size={14}/>}{copied === OLLAMA_INSTALL ? '已复制' : '复制'}</button></div>
      </div>}
      {pullFailed && unreachableRegistry(pullFailed) && <div className="install-command" role="note" aria-label="下载失败原因">
        {proxyCommand ? <>
          <span>Ollama 连不上模型仓库（hf.co）。Windows 的系统代理（{status?.systemProxy}）只对浏览器等生效，Ollama 服务不会使用它。在 PowerShell 运行下面的命令，再退出并重新打开 Ollama，然后点「下载模型」：</span>
          <div><code>{proxyCommand}</code><button type="button" className="secondary-button" aria-label="复制代理命令" onClick={() => copy(proxyCommand)}>{copied === proxyCommand ? <Check size={14}/> : <Copy size={14}/>}{copied === proxyCommand ? '已复制' : '复制'}</button></div>
        </> : <span>Ollama 连不上模型仓库（hf.co），可能需要代理。也可以用下面的「国内镜像下载」在浏览器里下载模型文件，再点「导入模型文件…」。</span>}
      </div>}
      <div className="translation-model-row">
        <span data-state={local?.installed ? 'ready' : local?.reachable ? 'missing' : 'offline'}>{pulling ? `${progress.status || '下载中'}${value !== undefined ? ` · ${value}%` : ''}`
          : checking && !status ? '正在检测…' : !local?.reachable ? `未检测到 Ollama（${local?.endpoint || endpoint}）` : local.installed ? `模型已就绪 · Ollama ${local.version}${gigabytes(local.size)}` : `Ollama ${local.version} 已运行，模型未下载`}</span>
        <button type="button" className="secondary-button" disabled={checking || pulling} onClick={refresh}>重新检测</button>
        {local?.installed
          ? <button type="button" className="primary-button" disabled={testing} onClick={test}>{testing ? '测试中…' : '测试翻译'}</button>
          : <button type="button" className="primary-button" aria-label="下载翻译模型" disabled={!local?.reachable || pulling} onClick={download}>{pulling ? '下载中…' : '下载模型'}</button>}
      </div>
      <div className="manual-download">
        <span>「下载模型」连不上时，可以在浏览器里下载 <b>Q4_K_M</b> 版本的 .gguf 文件（约 1.1 GB），再导入 Ollama：</span>
        <div>
          <button type="button" className="secondary-button" onClick={() => void window.music.openPage('hy-mt2-mirror').catch(error => notify(message(error)))}>国内镜像下载</button>
          <button type="button" className="secondary-button" onClick={() => void window.music.openPage('hy-mt2').catch(error => notify(message(error)))}>Hugging Face 下载</button>
          <button type="button" className="primary-button" aria-label="导入模型文件" disabled={!local?.reachable || pulling} onClick={importModel}>导入模型文件…</button>
        </div>
      </div>
      {pulling && <div className="translation-progress" role="progressbar" aria-label="翻译模型下载进度" aria-valuemin={0} aria-valuemax={100} aria-valuenow={value}><i style={{ width: `${value ?? 0}%` }}/></div>}
      <div className="settings-field translation-endpoint">
        <label><span>Ollama 地址</span><input aria-label="Ollama 地址" value={endpoint} spellCheck={false} onChange={event => setEndpoint(event.target.value)}/></label>
        <label><span>模型</span><input aria-label="翻译模型名称" value={model} spellCheck={false} onChange={event => setModel(event.target.value)}/></label>
        <button type="button" className="secondary-button" disabled={endpoint === (settings?.translateEndpoint || DEFAULT_ENDPOINT) && model === (settings?.translateModel || DEFAULT_MODEL)} onClick={saveLocal}>保存</button>
      </div>
    </>}
  </div>;
}
