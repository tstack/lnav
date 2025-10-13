class LnavExecError extends Error {
    constructor(message, details) {
        super(message);
        this.details = details;
    }
}

const lnav = {
    exec: async function (script) {
        try {
            const response = await fetch("/api/exec", { // POST to the current URL
                method: 'POST',
                headers: {
                    'Content-Type': 'text/x-lnav-script',
                },
                body: script,
            });

            const contentType = response.headers.get('Content-Type');
            if (!response.ok) {
                if (contentType.includes('application/json')) {
                    const jsonResponse = await response.json();
                    throw new LnavExecError(jsonResponse.msg, jsonResponse);
                }

                throw new Error(`HTTP error! status: ${response.status}`);
            }

            if (contentType.includes('application/json')) {
                const jsonResponse = await response.json();
                console.log('Server response:', jsonResponse);
                return jsonResponse;
            }

            const textResponse = await response.text();
            console.log('Server response:', textResponse);
            return textResponse;
        } catch (error) {
            console.error('Error sending data or receiving response:', error);
            throw error;
        }
    },

    poll: async function (prevState) {
        try {
            let req = {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify(prevState),
            };
            const response = await fetch("/api/poll", req);
            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }
            const jsonResponse = await response.json();
            console.log('Server response:', jsonResponse);
            return jsonResponse;
        } catch (error) {
            console.error('Error sending data or receiving response:', error);
            throw error;
        }
    }
};
