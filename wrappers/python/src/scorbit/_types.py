# Scorbit SDK
#
# (c) 2025 Spinner Systems, Inc. (DBA Scorbit), scorbit.io, All Rights Reserved
#
# MIT License
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

"""Shared data types used across the wrapper."""


class BundlePrice(object):
    """A credit bundle with pricing.

    Attributes:
        credits: Number of credits in this bundle.
        price: Effective price (sale or regular) as a decimal string.
        regular_price: Non-discounted price as a decimal string.
        sale_price: Discounted price as a decimal string, empty if no sale.
    """

    __slots__ = ("credits", "price", "regular_price", "sale_price")

    def __init__(self, credits=0, price="", regular_price="", sale_price=""):
        # type: (int, str, str, str) -> None
        self.credits = credits
        self.price = price
        self.regular_price = regular_price
        self.sale_price = sale_price

    def __repr__(self):
        # type: () -> str
        return "BundlePrice(credits={!r}, price={!r})".format(
            self.credits, self.price
        )


class PricingInfo(object):
    """Pricing configuration for the machine.

    Attributes:
        free_play: Whether the machine is in free play mode.
        payments_enabled: Whether payments are enabled.
        credit_price: Effective price per credit as a decimal string.
        credit_regular_price: Non-discounted price per credit.
        credit_sale_price: Sale price per credit, empty if no sale.
        bundles: List of :class:`BundlePrice` objects.
    """

    __slots__ = (
        "free_play",
        "payments_enabled",
        "credit_price",
        "credit_regular_price",
        "credit_sale_price",
        "bundles",
    )

    def __init__(
        self,
        free_play=False,
        payments_enabled=False,
        credit_price="",
        credit_regular_price="",
        credit_sale_price="",
        bundles=None,
    ):
        # type: (bool, bool, str, str, str, list) -> None
        self.free_play = free_play
        self.payments_enabled = payments_enabled
        self.credit_price = credit_price
        self.credit_regular_price = credit_regular_price
        self.credit_sale_price = credit_sale_price
        self.bundles = bundles if bundles is not None else []

    def __repr__(self):
        # type: () -> str
        return "PricingInfo(free_play={!r}, payments_enabled={!r}, credit_price={!r}, bundles={!r})".format(
            self.free_play, self.payments_enabled, self.credit_price, len(self.bundles)
        )


class PlayerInfo(object):
    """Player profile information.

    Attributes:
        id: The player's id in the Scorbit system.
        preferred_name: The player's preferred display name.
        name: The player's full name.
        initials: The player's initials.
        picture_url: URL to the player's profile picture.
        claim_deeplink: Claim URL for unclaimed player slots (empty if claimed).
    """

    __slots__ = (
        "id",
        "preferred_name",
        "name",
        "initials",
        "picture_url",
        "claim_deeplink",
        "_has_info",
    )

    def __init__(
        self,
        id="",
        preferred_name="",
        name="",
        initials="",
        picture_url="",
        claim_deeplink="",
        has_info=False,
    ):
        # type: (str, str, str, str, str, str, bool) -> None
        self.id = id
        self.preferred_name = preferred_name
        self.name = name
        self.initials = initials
        self.picture_url = picture_url
        self.claim_deeplink = claim_deeplink
        self._has_info = has_info

    def has_info(self):
        # type: () -> bool
        """Return True if the player slot is claimed and has profile info.

        When False, only :attr:`claim_deeplink` is meaningful.
        """
        return self._has_info

    def __repr__(self):
        # type: () -> str
        if self._has_info:
            return "PlayerInfo(id={!r}, preferred_name={!r})".format(
                self.id, self.preferred_name
            )
        return "PlayerInfo(claim_deeplink={!r})".format(self.claim_deeplink)
