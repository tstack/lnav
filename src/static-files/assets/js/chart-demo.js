import * as chartlib from 'https://esm.sh/chart.js@4.5.0?bundle-deps=';

// to register everything
chartlib.Chart.register(...chartlib.registerables);

async function getTopRequesters() {
    const data = await lnav.exec(`
;SELECT c_ip, COUNT(*) AS count
   FROM access_log
  GROUP BY c_ip
  ORDER BY count DESC
  LIMIT 10
:write-json-to -
`);

    new chartlib.Chart(
        document.getElementById('results'),
        {
            type: 'bar',
            data: {
                labels: data.map(row => row.c_ip),
                datasets: [
                    {
                        label: 'Top Requesters',
                        data: data.map(row => row.count)
                    }
                ]
            }
        }
    );
}

console.log('chart-demo.js loaded');
document.getElementById('top-requesters').addEventListener('click', getTopRequesters);
console.log('top-requesters button added');
