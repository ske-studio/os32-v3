"""A candidate's current documentation, parsed once per graph check."""
from collections import defaultdict
from dataclasses import dataclass
from html.parser import HTMLParser
from pathlib import Path
import posixpath
import re
import unicodedata
from urllib.parse import unquote, urlsplit
from tests.support.tracked import paths


def documents(tracked):
    return sorted(p for p in tracked if p.endswith(".md") and
                  ("/" not in p or p.startswith("docs/")) and
                  not p.startswith(("docs/archive/", "docs/hw/")))


class Anchors(HTMLParser):
    def __init__(self):
        super().__init__()
        self.names = set()

    def handle_starttag(self, tag, attrs):
        self.names.update(value for key, value in attrs
                          if value and (key == "id" or tag == "a" and key == "name"))


def slug(text):
    return "".join("-" if c == " " else c for c in text.strip().lower()
                   if c in " _-" or unicodedata.category(c)[0] in "LMN")


@dataclass(frozen=True)
class Edge:
    source: str
    line: int
    target: str
    fragment: str
    navigation: bool


def parse(name, parser):
    tokens = parser.parse(Path(name).read_text())
    anchors = Anchors()
    headings = set()
    edges = []
    for i, token in enumerate(tokens):
        if token.type == "html_block":
            anchors.feed(token.content)
        if token.type != "inline":
            continue
        children = token.children
        if i and tokens[i-1].type == "heading_open":
            title = "".join(t.content for t in children if t.type in ("text", "code_inline", "image"))
            base = slug(title)
            key = base
            suffix = 0
            while key in headings:
                suffix += 1
                key = f"{base}-{suffix}"
            headings.add(key)
        line = token.map[0] + 1
        preceding = ""
        for child in children:
            if child.type in ("softbreak", "hardbreak"):
                line += 1
                preceding = ""
            elif child.type == "html_inline":
                anchors.feed(child.content)
            elif child.type == "text":
                preceding += child.content
            elif child.type in ("link_open", "image"):
                url = urlsplit(child.attrGet("href" if child.type == "link_open" else "src"))
                if url.scheme or url.netloc:
                    continue
                raw = unquote(url.path)
                target = posixpath.normpath(posixpath.join(posixpath.dirname(name), raw)) if raw else name
                nav = preceding.endswith(("【案内】", "【経緯】")) or (
                    line <= 12 and re.match(r"\s*(上位|後続)[:：]", preceding))
                edges.append(Edge(name, line, target, unquote(url.fragment), bool(nav)))
    return headings | anchors.names, edges


def graph():
    from markdown_it import MarkdownIt
    parser = MarkdownIt("commonmark").enable("table")
    tracked = paths(Path.cwd())
    anchors = {}
    edges = defaultdict(list)
    for name in documents(tracked):
        anchors[name], edges[name] = parse(name, parser)
    return tracked, anchors, edges


def report(errors):
    from tests.support.media import Failure
    if errors:
        raise Failure("\n".join(sorted(set(errors))))


def headings_in(path):
    from markdown_it import MarkdownIt
    tokens = MarkdownIt().parse(path.read_text())
    return [tokens[i+1].content for i, token in enumerate(tokens) if token.type == "heading_open"]
