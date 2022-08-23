const fs = require('fs');
const category = process.argv[2];
const status = JSON.parse(process.argv[3]);

if (!fs.existsSync("./badges")) fs.mkdirSync("./badges");
if (!fs.existsSync("./badges/" + category)) fs.mkdirSync("./badges/" + category);
for (let e in status) {
    const path = "./badges/" + category + "/" + e;
    if (!fs.existsSync(path)) fs.mkdirSync(path);
    const ok = status[e] == "success";
    fs.writeFileSync(path + "/shields.json", JSON.stringify({
        "schemaVersion": 1,
        "label": e,
        "message": ok ? "Passing" : "Failing",
        "color": ok ? "brightgreen" : "red"
    }));
}
