async function testVersion() {
    let result = await lnav.version();
    document.getElementById('version-result').innerHTML =
        Prism.highlight(JSON.stringify(result, null, 2),
            Prism.languages.json, 'json');
}

document.getElementById('doVersion').addEventListener('click', testVersion);

let previousPollState = null;

async function testPoll() {
    let newPollState = await lnav.poll(previousPollState);
    document.getElementById('poll-time').innerText =
        new Date().toLocaleString();
    document.getElementById('poll-result').innerHTML =
        Prism.highlight(JSON.stringify(newPollState, null, 2),
            Prism.languages.json, 'json');
    previousPollState = newPollState.next_input;
}

document.getElementById('doPoll').addEventListener('click', testPoll);

async function testExec() {
    const script = document.getElementById('exec-input').value;
    try {
        const result = await lnav.exec(script);
        if (typeof result === 'string') {
            document.getElementById('exec-result').innerText = result;
            return;
        }
        document.getElementById('exec-result').innerHTML =
            Prism.highlight(JSON.stringify(result, null, 2),
                Prism.languages.json, 'json');
    } catch (error) {
        document.getElementById('exec-result').innerText = error.message;
    }
}

document.getElementById('doExec').addEventListener('click', testExec);
