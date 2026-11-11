import React from 'react';
import { onCaptionTooltip } from './bridge';
import { Tooltips } from './tooltips';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import './style.css';
const tooltips = new Tooltips();
const stopCaptionTips = onCaptionTooltip(tip => {
  tooltips.hide();
  if (!tip.text) return;
  // In the WebView2 caption, anchor the native maximize-button tooltip to the
  // same caption strip as the other two buttons instead of floating above it.
  const caption = document.querySelector<HTMLElement>('.web-caption')?.getBoundingClientRect();
  tooltips.show({ left: tip.left, width: tip.width, top: caption?.top ?? -1, height: caption?.height ?? 1 }, tip.text);
});
if (import.meta.hot) import.meta.hot.dispose(() => { stopCaptionTips(); tooltips.dispose(); });
createRoot(document.getElementById('root')!).render(<App/>);
