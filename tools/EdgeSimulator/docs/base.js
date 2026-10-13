// 手册生成公共helper
const fs = require("fs");
const { Document, Packer, Paragraph, TextRun, Table, TableRow, TableCell,
        Header, Footer, AlignmentType, HeadingLevel, BorderStyle, WidthType,
        ShadingType, PageNumber, PageBreak, ImageRun } = require("docx");

const FONT = "Microsoft YaHei";
const t = (text, opt = {}) => new TextRun({ text, font: FONT, size: 21, ...opt });
const p = (text, opt = {}) => new Paragraph({ spacing: { after: 80 }, children: Array.isArray(text) ? text : [t(text)], ...opt });
const h1 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_1, spacing: { before: 300, after: 160 }, children: [t(text, { bold: true, size: 30 })] });
const h2 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_2, spacing: { before: 220, after: 120 }, children: [t(text, { bold: true, size: 26 })] });
const h3 = (text) => new Paragraph({ heading: HeadingLevel.HEADING_3, spacing: { before: 160, after: 100 }, children: [t(text, { bold: true, size: 23 })] });
const code = (text) => new Paragraph({ spacing: { after: 60 }, shading: { fill: "F2F2F2", type: ShadingType.CLEAR },
  children: [t(text, { font: "Consolas", size: 19 })] });

// 表格：cols=列宽数组(DXA)，header=表头数组，rows=数据数组
function table(cols, header, rows) {
  const border = { style: BorderStyle.SINGLE, size: 1, color: "B0B0B0" };
  const borders = { top: border, bottom: border, left: border, right: border };
  const cell = (text, w, isHeader) => new TableCell({
    borders, width: { size: w, type: WidthType.DXA },
    shading: isHeader ? { fill: "DCE6F1", type: ShadingType.CLEAR } : undefined,
    margins: { top: 60, bottom: 60, left: 100, right: 100 },
    children: [new Paragraph({ children: [t(text, { size: 19, bold: !!isHeader })] })]
  });
  return new Table({
    width: { size: cols.reduce((a, b) => a + b, 0), type: WidthType.DXA },
    columnWidths: cols,
    rows: [
      new TableRow({ tableHeader: true, children: header.map((h, i) => cell(h, cols[i], true)) }),
      ...rows.map(r => new TableRow({ children: r.map((c, i) => cell(String(c), cols[i], false)) }))
    ]
  });
}

module.exports = { fs, Document, Packer, Paragraph, TextRun, Header, Footer, AlignmentType,
                   PageNumber, PageBreak, ImageRun, t, p, h1, h2, h3, code, table, img };

// 插图：path=PNG路径，w/h=显示宽高(px)
function img(path, w, h, alt) {
  const { Paragraph, ImageRun } = require("docx");
  return new Paragraph({ alignment: "center", spacing: { before: 120, after: 120 },
    children: [new ImageRun({ type: "png", data: fs.readFileSync(path),
      transformation: { width: w, height: h },
      altText: { title: alt, description: alt, name: alt } })] });
}
