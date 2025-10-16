function updateElementFromResponse(element, result) {
    let template = document.createElement('template');
    if (typeof result === 'string') {
        template.innerHTML = `<div>${result}</div>`;
        element.replaceWith(template.content.firstChild);
        return;
    }

    if (typeof result === 'object' || (Array.isArray(
        result) && result.length === 1)) {
        let found = false;
        for (const [key, value] of Object.entries(result)) {
            for (let element of document.getElementsByName(key)) {
                element.value = value;
                found = true;
            }
        }
        if (found) {
            return;
        }
    }

    template.innerHTML =
        `<pre class="language-json"><code class="language-json"></code></pre>`;
    let codeElement = template.content.querySelector('code');
    codeElement.innerHTML = Prism.highlight(JSON.stringify(result, null, 2),
        Prism.languages.json, 'json');
    element.replaceWith(template.content.firstChild);
}

async function evalLnavCode() {
    // The md4c-html code does not generate IDs for headings, so we need to
    // add them manually. It's easier to do this in JS than C.
    const headElements = document.querySelectorAll(
        'h1:not([id]), h2:not([id]), h3:not([id]), h4:not([id]), h5:not([id]), h6:not([id])');
    for (const headElement of headElements) {
        const newId = headElement.textContent.trim().replace(/\s+/g, '-')
            .toLowerCase();
        headElement.setAttribute('id', newId);
    }

    const replaceElements = document.querySelectorAll(
        'code.language-lnav.eval-and-replace');

    for (const element of replaceElements) {
        const code = element.textContent;
        try {
            const result = await lnav.exec(code);
            updateElementFromResponse(element.parentElement, result);
        } catch (error) {
            element.textContent = `Error: ${error.message}`;
        }
    }

    const onDemandElements = document.querySelectorAll(
        'code.language-lnav.eval-on-demand');

    let onDemandIndex = 0;
    for (const element of onDemandElements) {
        const code = element.textContent;
        const labelAttr = element.attributes.getNamedItem("data-label");
        let label = labelAttr ? labelAttr.value : "Execute";
        let buttonId = `on-demand-button-${onDemandIndex}`;
        let buttonTemplate = document.createElement('template');
        buttonTemplate.innerHTML =
            `<div><button id="${buttonId}">${label}</button><div id="${buttonId}-result"></div></div>`;
        element.parentElement.replaceWith(buttonTemplate.content.firstChild);
        let resultElement = document.getElementById(`${buttonId}-result`);
        onDemandIndex += 1;
        document.getElementById(buttonId)
            .addEventListener('click', async () => {
                try {
                    let inputElements = document.querySelectorAll("input");
                    let vars = {};
                    for (const input of inputElements) {
                        vars[input.name] = input.value;
                    }
                    const result = await lnav.exec(code, vars);
                    updateElementFromResponse(resultElement, result);
                } catch (error) {
                    resultElement.textContent = `Error: ${error.message}`;
                }
            });
    }

    const barChartElements = document.querySelectorAll(
        'code.language-lnav.eval-for-bar-chart');

    if (barChartElements.length > 0) {
        const chartlib = await import('https://esm.sh/chart.js@4.5.0?bundle-deps=');

        chartlib.Chart.register(...chartlib.registerables);
        for (const element of barChartElements) {
            const code = element.textContent;
            let loaderElement = document.createElement('template');
            loaderElement.innerHTML =
                `<div class="loader-container"><div class="loader"></div> Evaluating...</div>`;
            element.parentElement.insertBefore(loaderElement.content.firstChild,
                element);
            try {
                const result = await lnav.exec(code);
                if (Array.isArray(result)) {
                    let template = document.createElement('template');
                    template.innerHTML =
                        `<div class="bar-chart-container"><canvas></canvas></div>`;
                    let canvas = template.content.querySelector('canvas');
                    element.parentElement.replaceWith(
                        template.content.firstChild);
                    const datasets = result.slice(1).map(col => ({
                        label: col.name,
                        data: col.values,
                    }))
                    new chartlib.Chart(canvas, {
                        type: 'bar',
                        data: {
                            labels: result[0].values,
                            datasets,
                        },
                        options: {
                            scales: {
                                x: {
                                    title: {
                                        display: true,
                                        text: result[0].name,
                                    },
                                },
                            },
                        }
                    });
                }
            } catch (error) {
                element.textContent = `Error: ${error.message}`;
            }
        }
    }
}

evalLnavCode();
