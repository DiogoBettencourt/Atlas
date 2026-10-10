import { render } from "@testing-library/react";
import { describe, expect, it } from "vitest";
import { RichText } from "./RichText";

describe("RichText", () => {
  it("renders plain text without interpreting HTML", () => {
    const { container } = render(<RichText text={"hello <b>not bold</b>"} />);
    expect(container.querySelector("b")).toBeNull();
    expect(container.textContent).toBe("hello <b>not bold</b>");
  });

  it("renders fenced code blocks, dropping the language tag", () => {
    const { container } = render(<RichText text={"Run:\n```bash\ncmake -S . -B build\n```\nthen build."} />);
    const code = container.querySelector("pre code");
    expect(code?.textContent).toBe("cmake -S . -B build");
    expect(container.querySelectorAll("p")).toHaveLength(2);
  });

  it("keeps a first code line that is not a language tag", () => {
    const { container } = render(<RichText text={"```\nconst x = 1;\n```"} />);
    expect(container.querySelector("pre code")?.textContent).toBe("const x = 1;");
  });

  it("renders inline code", () => {
    const { container } = render(<RichText text={"use `--backend=openai` here"} />);
    expect(container.querySelector("code.inline-code")?.textContent).toBe("--backend=openai");
  });
});
