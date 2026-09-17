const { spawn } = require('child_process');
const http = require('http');

const chromePath = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const targetUrl = 'file:///D:/GestCam/tests/stream_10s.html';
const port = 9333;

function wait(ms) { return new Promise(res => setTimeout(res, ms)); }

async function run() {
    const chrome = spawn(chromePath, [
        `--remote-debugging-port=${port}`,
        '--use-fake-ui-for-media-stream',
        '--no-sandbox',
        '--disable-gpu',
        '--enable-logging=stderr',
        '--v=1',
        '--user-data-dir=C:\\Temp\\gestcam_test_profile'
    ]);

    chrome.stderr.on('data', d => {
        const str = d.toString();
        if (str.includes('video') || str.includes('capture') || str.includes('GestCam') || str.includes('DShow') || str.includes('Error')) {
            console.log('[CHROME STDERR]', str.trim());
        }
    });

    await wait(1500);

    const pages = await new Promise((resolve, reject) => {
        http.get(`http://127.0.0.1:${port}/json/list`, res => {
            let body = '';
            res.on('data', chunk => body += chunk);
            res.on('end', () => resolve(JSON.parse(body)));
        }).on('error', reject);
    });

    const page = pages.find(p => p.type === 'page') || pages[0];
    console.log('Page found:', page.title, page.url);

    const ws = new WebSocket(page.webSocketDebuggerUrl);

    let id = 1;
    function send(method, params = {}) {
        return new Promise((resolve) => {
            const reqId = id++;
            const handler = event => {
                const msg = JSON.parse(event.data);
                if (msg.id === reqId) {
                    ws.removeEventListener('message', handler);
                    resolve(msg.result);
                }
            };
            ws.addEventListener('message', handler);
            ws.send(JSON.stringify({ id: reqId, method, params }));
        });
    }

    await new Promise(r => ws.addEventListener('open', r));
    console.log('WebSocket open');

    await send('Runtime.enable');
    await send('Page.enable');

    ws.addEventListener('message', event => {
        const msg = JSON.parse(event.data);
        if (msg.method === 'Runtime.consoleAPICalled') {
            const text = msg.params.args.map(a => a.value || a.description || '').join(' ');
            console.log('[LOG]', text);
        }
        if (msg.method === 'Runtime.exceptionThrown') {
            console.error('[EXCEPTION]', msg.params.exceptionDetails);
        }
    });

    console.log('Navigating to', targetUrl);
    await send('Page.navigate', { url: targetUrl });

    // Wait 13 seconds
    for (let i = 0; i < 13; i++) {
        await wait(1000);
    }

    const res = await send('Runtime.evaluate', {
        expression: 'document.getElementById("status") ? document.getElementById("status").textContent : "no status element"'
    });

    console.log('STATUS:', res.result ? res.result.value : res);

    ws.close();
    chrome.kill();
}

run().catch(e => console.error(e));
