// Runs the remote page's picture converter in headless Chromium on a synthetic
// image and writes the wallpaper file it produces, for tools/check_remote_page.sh
// to verify with the device's own loader. Needs Playwright (npm i playwright).
//   node tools/check_remote_page.js out.stwp
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

(async () => {
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.setContent(fs.readFileSync(path.join(__dirname, 'remote_page.html'), 'utf8'));
  const b64 = await page.evaluate(async () => {
    const c = document.createElement('canvas');
    c.width = 640;
    c.height = 480;
    const x = c.getContext('2d');
    const g = x.createLinearGradient(0, 0, 640, 480);
    g.addColorStop(0, '#ff3366');
    g.addColorStop(0.5, '#33ff99');
    g.addColorStop(1, '#3355ff');
    x.fillStyle = g;
    x.fillRect(0, 0, 640, 480);
    x.fillStyle = '#ffffff';
    x.fillRect(100, 100, 200, 100);
    img = await createImageBitmap(c);
    document.getElementById('dark').value = 0;  // keep the colours for the check
    draw();
    const f = build();
    let s = '';
    for (let i = 0; i < f.length; i += 4096) s += String.fromCharCode.apply(null, f.subarray(i, i + 4096));
    return btoa(s);
  });
  fs.writeFileSync(process.argv[2], Buffer.from(b64, 'base64'));
  await browser.close();
  console.log('wrote', process.argv[2]);
})().catch(e => { console.error(e); process.exit(1); });
