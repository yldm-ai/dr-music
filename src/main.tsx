import React from 'react';
import { onCaptionTooltip } from './bridge';
import { Tooltips } from './tooltips';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import './style.css';
const tooltips = new Tooltips();
const stopCaptionTips = onCaptionTooltip(tip => {
  tooltips.hide();
  if (tip.text) tooltips.show({ left: tip.left, width: tip.width, top: -1, height: 1 }, tip.text);
});
if (import.meta.hot) import.meta.hot.dispose(() => { stopCaptionTips(); tooltips.dispose(); });
createRoot(document.getElementById('root')!).render(<App/>);
