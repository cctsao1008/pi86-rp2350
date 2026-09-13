"""WLINK map symbol parsing for the IA16 binary laboratory."""

from __future__ import annotations

from bisect import bisect_right
from dataclasses import dataclass
import re


_SYMBOL = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4,8})\+?\s+([^\s]+)",
    re.MULTILINE,
)


@dataclass(frozen=True, order=True)
class Symbol:
    address: int
    name: str


class SymbolTable:
    """Resolve WLINK segment:offset symbols to processor physical addresses."""

    def __init__(self, symbols: tuple[Symbol, ...]) -> None:
        self.symbols = tuple(sorted(symbols))
        self._addresses = tuple(symbol.address for symbol in self.symbols)
        self._by_name = {symbol.name: symbol for symbol in self.symbols}

    @classmethod
    def from_wlink_map(cls, text: str) -> "SymbolTable":
        symbols = tuple(
            Symbol((int(match.group(1), 16) << 4) + int(match.group(2), 16), match.group(3))
            for match in _SYMBOL.finditer(text)
        )
        if not symbols:
            raise ValueError("WLINK map contains no segment:offset symbols")
        return cls(symbols)

    def address(self, name: str) -> int:
        try:
            return self._by_name[name].address
        except KeyError as exc:
            raise ValueError(f"symbol not found: {name}") from exc

    def resolve(self, address: int) -> str | None:
        index = bisect_right(self._addresses, address) - 1
        if index < 0:
            return None
        symbol = self.symbols[index]
        delta = address - symbol.address
        return symbol.name if delta == 0 else f"{symbol.name}+0x{delta:X}"

    def span(self, name: str) -> tuple[int, int]:
        symbol = self._by_name.get(name)
        if symbol is None:
            raise ValueError(f"symbol not found: {name}")
        index = self.symbols.index(symbol)
        end = self.symbols[index + 1].address if index + 1 < len(self.symbols) else symbol.address + 1
        return symbol.address, max(symbol.address + 1, end)
