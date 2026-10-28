#pragma once
enum class LikeResult { Applied, AlreadyLiked, NoPlayer, Failed, ReadyUnliked };
// Uses only the currently selected Yandex Music tab. Never toggles an existing like off.
LikeResult likeYandexMusic(bool apply);
