// Build the offline Chinese manual from the Markdown files listed in its index.
// Run from any directory: node tools/build-zh-manual.cjs
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const sourceDir = path.join(root, 'docs', 'zh-cn');
const indexPath = path.join(sourceDir, 'index.md');
const homePath = path.join(sourceDir, 'home.md');
const outputPath = path.join(sourceDir, 'starfall.html');
const index = fs.readFileSync(indexPath, 'utf8').replace(/\r\n/g, '\n');
const home = fs.readFileSync(homePath, 'utf8').replace(/\r\n/g, '\n');
const oldManual = fs.readFileSync(path.join(root, 'docs', 'manual.html'), 'utf8');
const manualVersion = '0.40';
const oldStyle = oldManual.match(/<style>([\s\S]*?)<\/style>/);
if (!oldStyle) throw new Error('Cannot find the original manual stylesheet');

const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const pageId = p => 'page-' + p.replace(/\.md$/i, '').replace(/[^a-zA-Z0-9_-]+/g, '-');
const pages = [];
let chapter = '', group = '';
for (const line of index.split('\n')) {
  let m = line.match(/^### (.+)$/);
  if (m) { chapter = m[1]; group = ''; continue; }
  m = line.match(/^- ([^\[][^\n]*)$/);
  if (m) { group = m[1]; continue; }
  m = line.match(/^\s*- \[([^\]]+)\]\(([^)]+\.md)\)$/);
  if (!m) continue;
  if (line.startsWith('- ')) group = '';
  const file = path.resolve(sourceDir, m[2]);
  if (!file.startsWith(sourceDir + path.sep) || !fs.existsSync(file)) throw new Error('Missing manual page: ' + m[2]);
  pages.push({ title:m[1], file, relative:m[2], chapter, group });
}
const pageByPath = new Map(pages.map(p => [path.resolve(p.file), p]));
const posix = p => p.split(path.sep).join('/');
function linkTarget(raw, currentFile) {
  if (/^(https?:|mailto:|#)/i.test(raw)) return raw;
  const [part, hash] = raw.split('#');
  const resolved = path.resolve(path.dirname(currentFile), part);
  if (resolved === indexPath) return '#start' + (hash ? '--' + hash : '');
  const page = pageByPath.get(resolved);
  if (page) return '#' + pageId(page.relative) + (hash ? '--' + hash : '');
  return posix(path.relative(sourceDir, resolved)) + (hash ? '#' + hash : '');
}
function inline(text, currentFile) {
  // Tokenize code and links before escaping so field names are displayed literally.
  const tokens = [];
  let input = text.replace(/`([^`]+)`|\[([^\]]+)\]\(([^)]+)\)/g, (_, code, label, url) => {
    const rendered = code !== undefined ? '<code>' + esc(code) + '</code>' : '<a href="' + esc(linkTarget(url, currentFile)) + '">' + esc(label) + '</a>';
    return '\u0001' + (tokens.push(rendered) - 1) + '\u0002';
  });
  input = esc(input).replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  return input.replace(/\u0001(\d+)\u0002/g, (_, i) => tokens[Number(i)]);
}
function headingId(text) {
  return text.toLowerCase().replace(/[^\p{L}\p{N}\s_-]/gu, '').trim().replace(/\s+/g, '-');
}
function renderMarkdown(markdown, file, id, skipFirstHeading = false) {
  const lines = markdown.replace(/\r\n/g, '\n').split('\n');
  let html = '', i = 0, listDepth = 0;
  const closeLists = () => { while (listDepth > 0) { html += '</li></ul>'; listDepth--; } };
  while (i < lines.length) {
    const line = lines[i];
    if (!line.trim()) { i++; continue; }
    let m = line.match(/^```([^\s]*)/);
    if (m) {
      closeLists(); const lang = m[1] || 'text'; const body = []; i++;
      while (i < lines.length && !lines[i].startsWith('```')) body.push(lines[i++]);
      if (i === lines.length) throw new Error('Unclosed code block: ' + file);
      i++;
      html += '<figure><figcaption><span><b>' + esc(lang.toUpperCase()) + '</b> 参数格式</span><button class="copy" type="button">复制</button></figcaption><pre><code>' + esc(body.join('\n')) + '</code></pre></figure>';
      continue;
    }
    m = line.match(/^(#{1,6})\s+(.+)$/);
    if (m) {
      closeLists(); i++;
      if (skipFirstHeading && m[1].length === 1) { skipFirstHeading = false; continue; }
      const level = Math.min(4, m[1].length + 1);
      html += '<h' + level + ' id="' + esc(id + '--' + headingId(m[2])) + '">' + inline(m[2], file) + '</h' + level + '>';
      continue;
    }
    if (/^\|/.test(line) && i + 1 < lines.length && /^\|[\s:|-]+\|\s*$/.test(lines[i + 1])) {
      closeLists(); const rows = []; while (i < lines.length && /^\|/.test(lines[i])) rows.push(lines[i++]);
      const cells = row => row.trim().replace(/^\||\|$/g, '').split('|').map(x => x.trim());
      html += '<div class="table-wrap"><table><thead><tr>' + cells(rows[0]).map(c => '<th scope="col">' + inline(c, file) + '</th>').join('') + '</tr></thead><tbody>';
      for (const row of rows.slice(2)) html += '<tr>' + cells(row).map(c => '<td>' + inline(c, file) + '</td>').join('') + '</tr>';
      html += '</tbody></table></div>'; continue;
    }
    m = line.match(/^(\s*)-\s+(.+)$/);
    if (m) {
      const depth = Math.floor(m[1].length / 2) + 1;
      while (listDepth > depth) { html += '</li></ul>'; listDepth--; }
      if (listDepth === depth) html += '</li>';
      while (listDepth < depth) { html += '<ul>'; listDepth++; }
      html += '<li>' + inline(m[2], file); i++; continue;
    }
    closeLists(); const paragraph = [line]; i++;
    while (i < lines.length && lines[i].trim() && !/^(#{1,6}\s|```|\s*-\s|\|)/.test(lines[i])) paragraph.push(lines[i++]);
    // Markdown navigation belongs to the source files; the HTML has its own navigation.
    if (/^\[返回目录\]/.test(paragraph[0])) continue;
    html += '<p>' + inline(paragraph.join(' '), file) + '</p>';
  }
  closeLists(); return html;
}

let nav = '<div class="nav-home"><a href="#start">手册首页</a></div>';
let previousChapter = '', previousGroup = '', groupOpen = false;
for (const p of pages) {
  if (p.chapter !== previousChapter) {
    if (groupOpen) { nav += '</details>'; groupOpen = false; }
    if (previousChapter) nav += '</details>';
    nav += '<details class="nav-group"><summary class="group">' + esc(p.chapter) + '</summary>';
    previousChapter = p.chapter; previousGroup = '';
  }
  if (p.group !== previousGroup) {
    if (groupOpen) { nav += '</details>'; groupOpen = false; }
    if (p.group) {
      nav += '<details class="nav-subgroup"><summary class="subgroup">' + esc(p.group) + '</summary>';
      groupOpen = true;
    }
  }
  previousGroup = p.group;
  nav += '<a href="#' + pageId(p.relative) + '">' + esc(p.title) + '</a>';
}
if (groupOpen) nav += '</details>';
if (previousChapter) nav += '</details>';
const homeParts = home.split(/(?=^## \[)/m);
const homeIntro = renderMarkdown(homeParts.shift(), homePath, 'start', true);
const homeCards = homeParts.map(part => {
  const match = part.match(/^## \[([^\]]+)\]\(([^)]+)\)\n+([\s\S]*?)\s*$/);
  if (!match) throw new Error('Invalid homepage entry in docs/zh-cn/home.md');
  const dest = path.resolve(path.dirname(homePath), match[2]);
  if (!pageByPath.has(dest)) throw new Error('Homepage entry is not in the manual directory: ' + match[2]);
  return '<a class="card" href="' + esc(linkTarget(match[2], homePath)) + '"><strong>' + esc(match[1]) + '<i aria-hidden="true">→</i></strong><span>' + inline(match[3].replace(/\s+/g, ' '), homePath) + '</span></a>';
}).join('');
let articles = '<article id="start" data-title="手册首页"><div class="crumb">规则词典 <span>/</span> 首页</div><h1>StarfallLab 流火实验室</h1>' + homeIntro + '<h3>主要功能</h3><div class="cards">' + homeCards + '</div></article>';
for (const p of pages) {
  const id = pageId(p.relative), text = fs.readFileSync(p.file, 'utf8');
  articles += '<article id="' + id + '" data-title="' + esc(p.title) + '"><div class="crumb">' + esc(p.chapter) + (p.group ? ' <span>/</span> ' + esc(p.group) : '') + '</div><h2>' + esc(p.title) + '</h2>' + renderMarkdown(text, p.file, id, true) + '</article>';
}
const script = `
(() => {
  const nav=document.querySelector('.sidebar'), input=document.querySelector('#search'), articles=[...document.querySelectorAll('main article')], results=document.querySelector('.results'), list=results.querySelector('.list');
  function route(){const hash=decodeURIComponent(location.hash.slice(1)), id=hash.split('--')[0]||'start';const target=document.getElementById(id)||document.getElementById('start');articles.forEach(a=>a.classList.toggle('active',a===target));results.hidden=true;nav.querySelectorAll('[aria-current]').forEach(a=>a.removeAttribute('aria-current'));const link=[...nav.querySelectorAll('a')].find(a=>a.getAttribute('href')==='#'+target.id);if(link)link.setAttribute('aria-current','page');nav.classList.remove('open');if(hash.includes('--'))requestAnimationFrame(()=>document.getElementById(hash)?.scrollIntoView());else window.scrollTo(0,0);}
  function search(){const q=input.value.trim().toLowerCase();if(!q){route();return;}articles.forEach(a=>a.classList.remove('active'));results.hidden=false;list.replaceChildren();let count=0;for(const a of articles){const text=a.textContent.replace(/\\s+/g,' '),pos=text.toLowerCase().indexOf(q);if(pos<0)continue;count++;const link=document.createElement('a'),title=document.createElement('strong'),excerpt=document.createElement('span');link.href='#'+a.id;title.textContent=a.dataset.title;excerpt.textContent=text.slice(Math.max(0,pos-45),pos+110);link.append(title,excerpt);list.append(link)}results.querySelector('[role=status]').textContent='找到 '+count+' 个章节';}
  input.addEventListener('input',search);window.addEventListener('hashchange',route);document.querySelector('.menu').addEventListener('click',()=>nav.classList.toggle('open'));document.querySelector('#print').addEventListener('click',()=>window.print());document.addEventListener('keydown',e=>{if(e.key==='/'&&e.target!==input&&!e.ctrlKey&&!e.metaKey&&!e.altKey){e.preventDefault();input.focus()}if(e.key==='Escape'){input.value='';route()}});document.querySelectorAll('.copy').forEach(b=>b.addEventListener('click',async()=>{const code=b.closest('figure').querySelector('code');try{await navigator.clipboard.writeText(code.textContent);b.textContent='已复制'}catch{const range=document.createRange();range.selectNodeContents(code);const selection=getSelection();selection.removeAllRanges();selection.addRange(range);b.textContent='已选中，请手动复制'}setTimeout(()=>b.textContent='复制',1800)}));document.documentElement.classList.add('js');route();
})();`;
const extraStyle = '.nav-home{margin-bottom:15px}.nav-group>summary,.nav-subgroup>summary{cursor:pointer;list-style:none;user-select:none;border-radius:4px}.nav-group>summary::-webkit-details-marker,.nav-subgroup>summary::-webkit-details-marker{display:none}.nav-group>summary::before,.nav-subgroup>summary::before{content:"▸";display:inline-block;width:15px;color:#879493}.nav-group[open]>summary::before,.nav-subgroup[open]>summary::before{content:"▾"}.nav-group>summary:hover,.nav-subgroup>summary:hover{background:#f0f0e9}.nav-group>summary{padding:4px 8px;font-size:13px}.nav-subgroup>summary{padding:3px 8px 3px 17px;font-size:12px;color:#647477;font-weight:650}.sidebar a{padding-left:24px}.nav-subgroup a{padding-left:36px}.nav-home a{padding-left:9px}article ul{padding-left:1.5em}article li{margin:3px 0}article h4{font-size:16px;margin:24px 0 10px}';
const chapterStyle = '.sidebar .nav-group+.nav-group{margin-top:8px}.sidebar .nav-group>summary{display:flex;align-items:center;gap:5px;margin:0;padding:7px 10px;border:1px solid #e1e5df;background:#f3f5f0;color:#394d4d;font-size:13px;font-weight:700;line-height:1.5;transition:background-color .15s,border-color .15s}.sidebar .nav-group>summary::before{width:13px;flex:none;color:#82918c}.sidebar .nav-group>summary:hover{background:#e9eee8;border-color:#cbd7d0}.sidebar .nav-group[open]>summary{background:#f5eae4;border-color:#e9d5c8;color:#8d432d}.sidebar .nav-group[open]>summary::before{color:#b45b3d}.sidebar summary:focus-visible{outline:3px solid #cc754e;outline-offset:2px}.nav-subgroup:first-of-type{margin-top:5px}';
const hierarchyStyle = '.sidebar .nav-group>summary{font-size:15px}.sidebar .nav-subgroup>summary{font-size:14px}.sidebar .nav-group>a,.sidebar .nav-subgroup>a{font-size:13px}';
const output = '<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><link rel="icon" type="image/png" href="../../assets/brand/starfall-lab-comet-fragments.png"><title>StarfallLab 流火实验室</title><style>' + oldStyle[1] + extraStyle + chapterStyle + hierarchyStyle + '</style></head><body><a class="skip" href="#main">跳至正文</a><header class="top"><div class="bar"><a class="brand" href="#start"><img class="emblem" src="../../assets/brand/starfall-lab-comet-fragments.png" alt=""><span>StarfallLab 流火实验室</span></a><span class="version">v' + esc(manualVersion) + '</span><button class="menu" type="button">目录</button><div class="tools"><label class="searchbox"><span class="lens" aria-hidden="true"></span><input id="search" type="search" placeholder="搜索参数或章节" aria-label="搜索手册"><kbd>/</kbd></label><button id="print" type="button">打印</button></div></div></header><div class="frame"><nav class="sidebar" aria-label="手册目录">' + nav + '</nav><main id="main" tabindex="-1"><section class="results" hidden><div class="crumb">规则词典 / 搜索</div><h2>搜索结果</h2><p role="status"></p><div class="list"></div></section>' + articles + '</main></div><script>' + script + '</script></body></html>';
fs.writeFileSync(outputPath, output, 'utf8');
console.log('Built ' + path.relative(root, outputPath) + ' with ' + pages.length + ' pages.');
