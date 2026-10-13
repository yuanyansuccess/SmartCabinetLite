// 组装生成 Word 手册
const B = require("./base");
const part1 = require("./part1");
const part2 = require("./part2");
const { fs, Document, Packer, Header, Footer, PageNumber, t } = B;

const doc = new Document({
  styles: {
    default: { document: { run: { font: "Microsoft YaHei", size: 21 } } },
  },
  sections: [{
    properties: {
      page: {
        size: { width: 11906, height: 16838 },   // A4
        margin: { top: 1300, right: 1300, bottom: 1300, left: 1300 },
      },
    },
    headers: {
      default: new Header({ children: [new (require("docx").Paragraph)({
        children: [t("边缘计算机模拟器使用手册 — 智能工具柜接口测试", { size: 18, color: "808080" })] })] }),
    },
    footers: {
      default: new Footer({ children: [new (require("docx").Paragraph)({
        alignment: B.AlignmentType.CENTER,
        children: [t("第 ", { size: 18 }), new (require("docx").TextRun)({ children: [PageNumber.CURRENT], size: 18 }), t(" 页", { size: 18 })] })] }),
    },
    children: [...part1, ...part2],
  }],
});

Packer.toBuffer(doc).then(buf => {
  // 先落 ASCII 名（批处理检查用），bat 再复制为中文名发布
  fs.writeFileSync("E:\\360MoveData\\Users\\25007\\Desktop\\EdgeSimulator_manual.docx", buf);
  console.log("OK " + buf.length);
});