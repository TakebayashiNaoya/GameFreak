#define _CRT_SECURE_NO_WARNINGS
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
using namespace std;


/**
 * 【入力内容】
 * N：野生のポケモンの数
 *
 * 1匹目：	H[1]（体力）
 *			C[1]（捕獲可能HP）
 *			V[1]（経験値）
 *			A[1][0] B[1][0]（1つ目の技の威力と回数）
 *			A[1][1] B[1][1]（2つ目の技の威力と回数）
 * ～
 * i匹目：	H[i]（体力）
 *			C[i]（捕獲可能HP）
 *			V[i]（経験値）
 *			A[i][0] B[i][0]（1つ目の技の威力と回数）
 *			A[i][1] B[i][1]（2つ目の技の威力と回数）
 *
 * 最初の手持ちのポケモン：	A[0][0] B[0][0]（1つ目の技の威力と回数）
 *							A[0][1] B[0][1]（2つ目の技の威力と回数）
 */


 //=============================================================================
 // 定数定義
 //=============================================================================
namespace
{
	// 技の数（1匹あたり固定で2つ）
	constexpr int MOVE_COUNT = 2;
	// 手持ちポケモンの最大数
	constexpr int MAX_HAND_SIZE = 6;
	// 倒すスコアの正規化係数（最大 exp/maxHp = 5000/500 = 10.0）
	constexpr float DEFEAT_SCORE_NORM = 10.0f;
	// 捕まえるスコアの正規化係数（最大 totalDamage - catchCost = 3750 - 375 = 3375）
	constexpr float CATCH_SCORE_NORM = 3375.0f;
	// 提出時コメントアウト部分：処理するファイルインデックスの最大値
	constexpr int FILE_INDEX_MAX = 4;
	// 提出時コメントアウト部分：ファイルパス文字列のバッファサイズ
	constexpr int PATH_BUFFER_SIZE = 64;
}


//=============================================================================
// データ構造
//=============================================================================

/**
 * ポケモンの現在の居場所を定義
 */
enum class Location : uint8_t
{
	WILD = 0,		// 野生
	HAND,			// 手持ち
	BOX,			// ボックス
	FAINTED,		// ひんし
	STOCKED			// C以下に削られ捕獲待ちの野生
};

/**
 * ポケモンの静的な情報を管理する構造体
 * 入力から読み込まれ、ゲーム中に変化しない情報を保持する
 */
struct StaticPokemonData
{
	int maxHp = 0;					// 最大体力
	int captureHp = 0;				// 捕獲可能HP（これ以下で捕獲できる）
	int exp = 0;					// 倒したときに得られる経験値
	int movePower[MOVE_COUNT] = { 0,0 };		// 技の威力
	int maxMoveCount[MOVE_COUNT] = { 0,0 };	// 技の最大使用回数
	int totalDamage = 0;			// 全技の合計ダメージ（事前計算済み）
	float score = 0.0f;				// 優先度スコア（大きいほど「倒す優先」、小さいほど「捕まえる優先」）
};

/**
 * ポケモンの動的な情報を管理する構造体
 * ゲームの進行に伴って変化する情報を保持する
 */
struct DynamicPokemonData
{
	int currentHp = 0;						// 現在の体力
	int remainingMoveCount[MOVE_COUNT] = { 0,0 };	// 現在の技の残り使用回数
	Location location = Location::WILD;		// 現在の居場所
};

/**
 * ゲームの進行状況（動的ステート）をまとめた構造体
 * 状態を更新する各関数に参照渡しで引き回すことで、main関数の肥大化を防ぐ
 */
struct GameState
{
	vector<DynamicPokemonData> pokemons;
	vector<int> handIds;
	vector<int> defeatTargets;
	vector<int> catchTargets;

	// フェーズ1の攻撃計画
	bool bIsPhase1Complete = false;
	// catchPlanTargetId: 現在ストックを積んでいる対象のID
	int catchPlanTargetId = -1;
	// catchPlan: 残りの技の使用順リスト {attackerId, moveIndex}
	vector<pair<int, int>> catchPlan;

	// 再戦略決定済みフラグ
	// ストック枯渇時に1回だけ再決定し、次の捕獲が発生するまで再決定しない
	bool bHasRedecided = false;
};


//=============================================================================
// 事前計算・ユーティリティ関数
//=============================================================================

/**
 * @brief 静的データから全技の合計ダメージと優先度スコアを事前計算する関数
 * @details
 *	倒すスコア     = exp / maxHp（正規化：最大 5000/500=10.0）
 *	捕まえるスコア = (totalDamage - catchCost) / 3375（正規化：最大 3375）
 *	最終スコア     = 倒すスコア - 捕まえるスコア
 *	スコアが大きいほど「倒す優先」、小さいほど「捕まえる優先」
 * @param p 計算対象の静的データ
 */
void CalcPrecalc(StaticPokemonData& p)
{
	p.totalDamage = (p.movePower[0] * p.maxMoveCount[0]) + (p.movePower[1] * p.maxMoveCount[1]);
	float defeatScore = static_cast<float>(p.exp) / p.maxHp / DEFEAT_SCORE_NORM;
	float catchScore = static_cast<float>(p.totalDamage - (p.maxHp - p.captureHp)) / CATCH_SCORE_NORM;
	p.score = defeatScore - catchScore;
}

/**
 * @brief 手持ち全員の残り技の合計ダメージを計算する関数
 * @param state    ゲームの動的ステート
 * @param baseData 静的データ
 * @return 合計ダメージ
 */
int CalcHandDamage(const GameState& state, const vector<StaticPokemonData>& baseData)
{
	int total = 0;
	for (int h : state.handIds) {
		for (int m = 0; m < MOVE_COUNT; ++m) {
			total += baseData[h].movePower[m] * state.pokemons[h].remainingMoveCount[m];
		}
	}
	return total;
}

/**
 * @brief 手持ちに技が1つでも残っているかどうかを確認する関数
 * @param state  ゲームの動的ステート
 * @return 残っていれば true
 */
bool HasAnyMove(const GameState& state)
{
	for (int h : state.handIds)
	{
		for (int m = 0; m < MOVE_COUNT; ++m) {
			if (state.pokemons[h].remainingMoveCount[m] > 0) return true;
		}
	}
	return false;
}


//=============================================================================
// 戦略決定関数
//=============================================================================

/**
 * @brief スコア降順にソートされたIDリストと利用可能ダメージから、倒す候補数を決定する関数
 * @details
 *	前k匹を倒し、残りを捕まえる方針において
 *	「利用可能ダメージ >= 必要ダメージ」を満たす最大のkを返す。
 *	利用可能ダメージ = availableDamage + catchBonusSuffix[k]
 *	必要ダメージ     = defeatCostPrefix[k] + catchCostSuffix[k]
 *	statePtr が nullptr のとき maxHp ベース、非nullptr のとき currentHp ベースでコストを計算する。
 * @param sortedRank      対象ポケモンのIDリスト（スコア降順ソート済み）
 * @param baseData        静的データ
 * @param availableDamage 現在の手持ち全員の残り技の合計ダメージ
 * @param statePtr        動的ステートへのポインタ（nullptr のとき maxHp ベースで計算）
 * @return 倒す候補の数
 */
int CalcDefeatCount(const vector<int>& sortedRank, const vector<StaticPokemonData>& baseData, int availableDamage, const GameState* statePtr)
{
	int rankCount = (int)sortedRank.size();

	// 先頭k匹を倒すのに必要なダメージの累積和
	vector<int> defeatCostPrefix(rankCount + 1, 0);
	// k匹目以降を捕まえるのに必要なダメージの後方和
	vector<int> catchCostSuffix(rankCount + 1, 0);
	// k匹目以降を捕まえたときに得られる技ダメージの後方和
	vector<int> catchBonusSuffix(rankCount + 1, 0);

	for (int i = 0; i < rankCount; ++i)
	{
		int id = sortedRank[i];
		// 倒すコスト：statePtr が非nullptr なら currentHp ベース、nullptr なら maxHp ベース
		int hp = (statePtr != nullptr) ? statePtr->pokemons[id].currentHp : baseData[id].maxHp;
		defeatCostPrefix[i + 1] = defeatCostPrefix[i] + hp;
	}

	for (int i = rankCount - 1; i >= 0; --i)
	{
		int id = sortedRank[i];
		int hp = (statePtr != nullptr) ? statePtr->pokemons[id].currentHp : baseData[id].maxHp;
		// 捕まえるコスト：捕獲圏内まで削るのに必要なダメージ（すでに圏内なら0）
		int catchCost = max(0, hp - baseData[id].captureHp);
		catchCostSuffix[i] = catchCostSuffix[i + 1] + catchCost;
		catchBonusSuffix[i] = catchBonusSuffix[i + 1] + baseData[id].totalDamage;
	}

	// 「利用可能ダメージ >= 必要ダメージ」を満たす最大の k を探す
	int defeatCount = 0;
	for (int k = 0; k <= rankCount; ++k)
	{
		int totalAvailable = availableDamage + catchBonusSuffix[k];
		int requiredDamage = defeatCostPrefix[k] + catchCostSuffix[k];

		if (totalAvailable >= requiredDamage) {
			defeatCount = k;
		}
		else {
			break;
		}
	}
	return defeatCount;
}

/**
 * @brief ストック枯渇時に残りWILDポケモンを対象として戦略を再決定する関数
 * @details
 *	まだ WILD のポケモンをスコア降順でソートし直し、
 *	現在の手持ちの残りダメージと currentHp ベースのコストで CalcDefeatCount を再実行する。
 *	defeatTargets / catchTargets / bIsPhase1Complete / catchPlan をリセットする。
 *	捕獲が1回発生するたびに bHasRedecided が false にリセットされるため、
 *	ストック枯渇のたびに再決定が走る（同一枯渇状態での二重実行は防ぐ）。
 * @param state             ゲームの動的ステート
 * @param baseData          静的データ
 * @param totalPokemonCount 野生のポケモンの数
 */
void ReDecideStrategy(GameState& state, const vector<StaticPokemonData>& baseData, int totalPokemonCount)
{
	// まだ WILD のポケモンを列挙してスコア降順でソート
	vector<int> wildRemaining;
	for (int i = 1; i <= totalPokemonCount; i++)
	{
		if (state.pokemons[i].location == Location::WILD) {
			wildRemaining.push_back(i);
		}
	}

	if (wildRemaining.empty())
	{
		// WILD が残っていなければターゲットをすべてクリア
		state.defeatTargets.clear();
		state.catchTargets.clear();
		state.bIsPhase1Complete = true;
		state.catchPlanTargetId = -1;
		state.catchPlan.clear();
		return;
	}

	sort(wildRemaining.begin(), wildRemaining.end(), [&](int a, int b) {
		return baseData[a].score > baseData[b].score;
		});

	// currentHp ベースで再戦略決定
	int currentDamage = CalcHandDamage(state, baseData);
	int defeatCount = CalcDefeatCount(wildRemaining, baseData, currentDamage, &state);

	// ターゲットを更新
	state.defeatTargets.assign(wildRemaining.begin(), wildRemaining.begin() + defeatCount);
	state.catchTargets.assign(wildRemaining.begin() + defeatCount, wildRemaining.end());

	// フェーズ1の状態をリセット
	state.bIsPhase1Complete = state.catchTargets.empty();
	state.catchPlanTargetId = -1;
	state.catchPlan.clear();
}

/**
 * @brief 手持ち全員の技の中から最大ダメージの技を選ぶ関数
 * @param state    ゲームの動的ステート
 * @param baseData 静的データ
 * @return { attackerId, moveIndex }　使える技がなければ { -1, -1 }
 */
pair<int, int> SelectBestAttackMove(const GameState& state, const vector<StaticPokemonData>& baseData)
{
	// 最大ダメージ
	int bestDamage = -1;
	// 威力が最大の技を持つポケモンID
	int bestAttacker = -1;
	// 技のインデックス
	int bestMoveIndex = -1;

	for (int h : state.handIds)
	{
		for (int m = 0; m < MOVE_COUNT; ++m)
		{
			if (state.pokemons[h].remainingMoveCount[m] > 0 && baseData[h].movePower[m] > bestDamage)
			{
				bestDamage = baseData[h].movePower[m];
				bestAttacker = h;
				bestMoveIndex = m;
			}
		}
	}

	return { bestAttacker, bestMoveIndex };
}

/**
 * @brief 捕獲圏内まで削り切るための技の使用順を計画する関数
 * @details
 *	targetId の currentHp を起点に DP で到達可能な HP を列挙し、
 *	捕獲可能範囲 [1, captureHp] の中で最も C に近い HP への
 *	技の使用順リストを返す。削り切れない場合は空リストを返す。
 * @param state    ゲームの動的ステート
 * @param baseData 静的データ
 * @param targetId 削り対象のポケモンID
 * @return {attackerId, moveIndex} のリスト（先頭から順に実行する）
 */
vector<pair<int, int>> PlanCatchSequence(const GameState& state, const vector<StaticPokemonData>& baseData, int targetId)
{
	// 今回のターゲットの今の体力
	int currentHp = state.pokemons[targetId].currentHp;
	// 今回のターゲットの捕獲可能HP
	int captureHp = baseData[targetId].captureHp;

	// すでに捕獲圏内なら手順不要
	if (currentHp >= 1 && currentHp <= captureHp) return{};

	// DP：各HPに到達するための「最後に使った技」を記録
	// prev[hp] = {attackerId, moveIndex}（未到達なら {-1, -1}）
	vector<pair<int, int>> prev(currentHp + 1, { -1, -1 });
	vector<bool> bIsReachable(currentHp + 1, false);
	bIsReachable[currentHp] = true;

	for (int h : state.handIds)
	{
		for (int m = 0; m < MOVE_COUNT; ++m)
		{
			int power = baseData[h].movePower[m];
			int count = state.pokemons[h].remainingMoveCount[m];
			if (count == 0) continue;

			for (int k = 0; k < count; ++k)
			{
				// コピーを使って同一技の連鎖加算を防ぐ
				vector<bool> next = bIsReachable;
				for (int hp = power + 1; hp <= currentHp; ++hp)
				{
					if (bIsReachable[hp] && !next[hp - power])
					{
						next[hp - power] = true;
						prev[hp - power] = { h, m };
					}
				}
				bIsReachable = next;
			}
		}
	}

	// 捕獲可能範囲 [1, captureHp] の中で最もCに近い（無駄が少ない）到達点を探す
	int bestHp = -1;
	for (int hp = captureHp; hp >= 1; --hp)
	{
		if (bIsReachable[hp]) { bestHp = hp; break; }
	}

	// 到達不可能なら削り切れない
	if (bestHp == -1) return{};

	// bestHp に至るまでの技の使用順を逆順に復元して反転
	vector<pair<int, int>> plan;
	int hp = bestHp;
	while (hp != currentHp)
	{
		auto [attackerId, moveIndex] = prev[hp];
		plan.push_back({ attackerId, moveIndex });
		hp += baseData[attackerId].movePower[moveIndex];
	}
	reverse(plan.begin(), plan.end());
	return plan;
}


//=============================================================================
// アクション処理
//=============================================================================

/**
 * @brief	ステップ1：ストック済みポケモンを捕まえる
 * @details 捕まえられるポケモンがいない かつ bIsPhase1Complete の場合は戦略を再決定する。
 *			捕獲または再決定を実行した場合は true を返す。
 */
bool TryCatchStocked(GameState& state, const vector<StaticPokemonData>& baseData, int totalPokemonCount)
{
	// 手持ちが最大数に達していれば捕まえられない
	if ((int)state.handIds.size() >= MAX_HAND_SIZE) return false;

	// 捕まえた後に残る WILD の {currentHp, captureHp} リスト（有用性判定用）
	vector<pair<int, int>> wildForUseful;
	for (int targetId : state.catchTargets)
	{
		if (state.pokemons[targetId].location != Location::WILD) continue;
		wildForUseful.push_back({ state.pokemons[targetId].currentHp, baseData[targetId].captureHp });
	}

	// 捕獲対象を選ぶ
	// 優先：捕まえた後に残りWILDの技圏内に届くポケモン（有用なポケモン）
	// 次点：それ以外のストックポケモン（fallback）

	// 捕まえるポケモンのID（見つからなければ -1）
	int catchId = -1;
	// フォールバック用のID（有用なポケモンが見つからなかったときに捕まえる）
	int fallbackId = -1;

	// 捕まえられるポケモンを探す
	for (int targetId : state.catchTargets)
	{
		Location loc = state.pokemons[targetId].location;

		// ストック済みかつ捕獲可能HP以下のWILDのみ捕まえ候補になる
		bool bIsStock = (loc == Location::STOCKED) ||
			(loc == Location::WILD &&
				state.pokemons[targetId].currentHp >= 1 &&
				state.pokemons[targetId].currentHp <= baseData[targetId].captureHp);

		// ストック済みでなければ候補にしない
		if (!bIsStock) continue;
		// ストック済みであればフォールバック候補には入れる
		if (fallbackId == -1) {
			fallbackId = targetId;
		}

		// 捕まえたポケモンの技で残りWILDを捕獲圏内に削れるなら「有用」と判定する
		bool bIsUseful = wildForUseful.empty();
		// 技のループを回す前に、wildForUseful が空なら有用とみなす（残りWILDがいないならどれを捕まえても同じ）
		for (auto& [wildHp, wildC] : wildForUseful)
		{
			for (int m = 0; m < MOVE_COUNT; ++m)
			{
				// 捕まえたポケモンの技で残りWILDを捕獲圏内に削れるか
				int power = baseData[targetId].movePower[m];
				int hpAfter = wildHp - power;
				// 技で削った後のHPが捕獲圏内に入るなら有用とみなす
				if (hpAfter >= 1 && hpAfter <= wildC) {
					bIsUseful = true;
					break;
				}
			}
			// 1つでも有用な技があれば十分なのでループを抜ける
			if (bIsUseful) break;
		}

		// 有用なポケモンが見つかったらそれを捕まえる
		if (bIsUseful) {
			catchId = targetId;
			break;
		}
	}

	// 捕まえるポケモンが見つからなければフォールバックを捕まえる
	if (catchId == -1) catchId = fallbackId;

	// 捕まえるポケモンが見つかれば捕獲実行
	if (catchId != -1)
	{
		// 捕獲実行
		state.pokemons[catchId].location = Location::HAND;
		for (int m = 0; m < MOVE_COUNT; ++m) {
			state.pokemons[catchId].remainingMoveCount[m] = baseData[catchId].maxMoveCount[m];
		}
		state.handIds.push_back(catchId);
		cout << 2 << " " << catchId << "\n";

		// 捕獲が発生したので再戦略フラグをリセット（次のストック枯渇時に再決定できる）
		state.bHasRedecided = false;
		return true;
	}

	//---------------------------------------------------------------------
	// ストックが尽きており bIsPhase1Complete かつ未再決定の場合は戦略を再決定する
	// bHasRedecided が false のときのみ実行（1枯渇サイクルに1回限定）
	//---------------------------------------------------------------------
	if (state.bIsPhase1Complete && !state.bHasRedecided)
	{
		ReDecideStrategy(state, baseData, totalPokemonCount);
		state.bHasRedecided = true;
		return true;
	}

	return false;
}

/**
 * @brief ステップ2：技が尽きた手持ちをボックスへ送る
 * @details catchPlan 実行中は手持ちを維持する（途中でボックスに送ると計画が破綻するため）
 */
void BoxDepletedHand(GameState& state)
{
	// catchPlan が空でない（フェーズ1の攻撃計画が進行中）なら、手持ちを維持して計画を完遂させる
	if (!state.catchPlan.empty()) return;

	// 技が尽きた手持ちをボックスへ送る
	vector<int> newHandIds;
	for (int h : state.handIds)
	{
		// 技がすべて尽きているかどうかを確認
		bool bIsAllDepleted = true;
		for (int m = 0; m < MOVE_COUNT; ++m)
		{
			if (state.pokemons[h].remainingMoveCount[m] > 0)
			{
				bIsAllDepleted = false;
				break;
			}
		}

		// 技がすべて尽きているならボックスへ送る
		if (bIsAllDepleted)
		{
			// 技切れ → ボックスへ送る
			state.pokemons[h].location = Location::BOX;
			cout << 3 << " " << h << "\n";
		}
		else
		{
			newHandIds.push_back(h);
		}
	}
	state.handIds = newHandIds;
}

/**
 * @brief フェーズ1：catchTargets をC以下に削ってストックする
 * @details catchPlan に従って技を使用し、計画通りに削り切る
 */
bool ExecutePhase1Attack(GameState& state, const vector<StaticPokemonData>& baseData)
{
	// catchPlanTargetId が無効（ストック完了・ひんし）になった場合はリセット
	if (state.catchPlanTargetId != -1 && state.pokemons[state.catchPlanTargetId].location != Location::WILD)
	{
		state.catchPlanTargetId = -1;
		state.catchPlan.clear();
	}

	// catchPlan が空なら新しい対象を探して計画を立てる
	if (state.catchPlan.empty())
	{
		// 新しい対象を探して計画を立てる
		for (int targetId : state.catchTargets)
		{
			// 対象が WILD でなければスキップ
			if (state.pokemons[targetId].location != Location::WILD) continue;

			// 対象に対する攻撃計画を立てる
			auto plan = PlanCatchSequence(state, baseData, targetId);
			// 計画が立てられればそれを採用してループを抜ける
			if (!plan.empty())
			{
				state.catchPlanTargetId = targetId;
				state.catchPlan = plan;
				break;
			}
		}
	}

	// catchPlan に従って攻撃
	if (!state.catchPlan.empty())
	{
		auto [attackerId, moveIndex] = state.catchPlan.front();
		state.catchPlan.erase(state.catchPlan.begin());

		// 技を使う
		int attackedTargetId = state.catchPlanTargetId;
		int damage = baseData[attackerId].movePower[moveIndex];
		state.pokemons[state.catchPlanTargetId].currentHp -= damage;
		state.pokemons[attackerId].remainingMoveCount[moveIndex]--;

		// ダメージを与えた後の状態に応じて、ひんし・ストック・継続を判定
		if (state.pokemons[state.catchPlanTargetId].currentHp <= 0)
		{
			// ひんし → 計画をリセット
			state.pokemons[state.catchPlanTargetId].location = Location::FAINTED;
			state.catchPlanTargetId = -1;
			state.catchPlan.clear();
		}
		else if (state.pokemons[state.catchPlanTargetId].currentHp <= baseData[state.catchPlanTargetId].captureHp)
		{
			// 捕獲圏内に到達 → ストック状態にして計画をリセット
			state.pokemons[state.catchPlanTargetId].location = Location::STOCKED;
			state.catchPlanTargetId = -1;
			state.catchPlan.clear();
		}

		cout << 1 << " " << attackerId << " " << attackedTargetId << " " << (moveIndex + 1) << "\n";
		return true;
	}

	// フェーズ1で計画が立てられない場合は defeatTargets を攻撃して手持ちを入れ替える
	for (int targetId : state.defeatTargets)
	{
		// 対象が WILD でなければスキップ
		if (state.pokemons[targetId].location != Location::WILD) continue;

		// 技を選んで攻撃
		auto [attackerId, moveIndex] = SelectBestAttackMove(state, baseData);

		// 技が選べない（手持ちの技がすべて尽きている）場合は攻撃できないのでスキップ
		if (attackerId != -1)
		{
			// 技を使う
			int damage = baseData[attackerId].movePower[moveIndex];
			state.pokemons[targetId].currentHp -= damage;
			state.pokemons[attackerId].remainingMoveCount[moveIndex]--;
			if (state.pokemons[targetId].currentHp <= 0) {
				state.pokemons[targetId].location = Location::FAINTED;
			}

			cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
			return true;
		}
		break;
	}
	return false;
}

/**
 * @brief フェーズ2：defeatTargets を倒す
 * @details （ストックがあればステップ1で並行して捕獲される）
 */
bool ExecutePhase2Attack(GameState& state, const vector<StaticPokemonData>& baseData)
{
	// defeatTargets を攻撃して倒す
	for (int targetId : state.defeatTargets)
	{
		// 対象が WILD でなければスキップ
		if (state.pokemons[targetId].location != Location::WILD) continue;

		// 技を選んで攻撃
		auto [attackerId, moveIndex] = SelectBestAttackMove(state, baseData);

		// 技が選べない（手持ちの技がすべて尽きている）場合は攻撃できないのでスキップ
		if (attackerId != -1)
		{
			// 技を使う
			int damage = baseData[attackerId].movePower[moveIndex];
			state.pokemons[targetId].currentHp -= damage;
			state.pokemons[attackerId].remainingMoveCount[moveIndex]--;
			if (state.pokemons[targetId].currentHp <= 0) {
				state.pokemons[targetId].location = Location::FAINTED;
			}

			cout << 1 << " " << attackerId << " " << targetId << " " << (moveIndex + 1) << "\n";
			return true;
		}
		break;
	}
	return false;
}

/**
 * @brief ステップ3：技を使う行動のメインエントリ
 */
bool ExecuteAttack(GameState& state, const vector<StaticPokemonData>& baseData)
{
	// bIsPhase1Complete フラグを再チェック（ストック積みが完了したか確認）
	if (!state.bIsPhase1Complete)
	{
		// すべての catchTargets が WILD でなくなったらフェーズ1完了
		state.bIsPhase1Complete = true;

		// まだ catchTargets に WILD が残っているならフェーズ1は続行
		for (int targetId : state.catchTargets)
		{
			if (state.pokemons[targetId].location == Location::WILD)
			{
				state.bIsPhase1Complete = false;
				break;
			}
		}
	}

	// 早期リターンによりネストを浅くする
	if (!state.bIsPhase1Complete)
	{
		return ExecutePhase1Attack(state, baseData);
	}
	else
	{
		return ExecutePhase2Attack(state, baseData);
	}
}


//=============================================================================
// メイン関数
//=============================================================================
int main()
{
	// 提出時、以下はコメントアウト
	/** ここから */
	//for (int fileIndex = 0; fileIndex <= FILE_INDEX_MAX; fileIndex++)
	//{
	//	char inputPath[PATH_BUFFER_SIZE];
	//	char outputPath[PATH_BUFFER_SIZE];
	//	sprintf(inputPath, "入力ファイル/%04d.txt", fileIndex);
	//	sprintf(outputPath, "出力ファイル/%04d.out", fileIndex);
	//	ifstream is(inputPath);
	//	ofstream os(outputPath);
	//	streambuf* oldIn = cin.rdbuf(is.rdbuf());
	//	streambuf* oldOut = cout.rdbuf(os.rdbuf());
		/** ここまで */

	////////////////////////////////////////////////////
	// 変数の宣言や、入力の受け取りなどを行うフェーズ //
	////////////////////////////////////////////////////

	// ポケモンの総数を入力から受け取る
	int totalPokemonCount;
	cin >> totalPokemonCount;

	// ポケモンの静的データを格納するベクターを宣言し、入力から受け取る
	vector<StaticPokemonData> baseData(totalPokemonCount + 1);
	for (int i = 1; i <= totalPokemonCount; i++)
	{
		cin >> baseData[i].maxHp >> baseData[i].captureHp >> baseData[i].exp
			>> baseData[i].movePower[0] >> baseData[i].maxMoveCount[0]
			>> baseData[i].movePower[1] >> baseData[i].maxMoveCount[1];
		CalcPrecalc(baseData[i]);
	}
	cin >> baseData[0].movePower[0] >> baseData[0].maxMoveCount[0]
		>> baseData[0].movePower[1] >> baseData[0].maxMoveCount[1];
	// ID=0 は手持ちのポケモンのデータ（maxHp, captureHp, exp は未使用）
	CalcPrecalc(baseData[0]);


	/////////////////////////////
	// 初期状態の構築           //
	/////////////////////////////


	GameState state;
	state.pokemons.resize(totalPokemonCount + 1);
	// 全ポケモンの currentHp を maxHp で初期化し、技の残り回数を maxMoveCount で初期化する
	for (int i = 0; i <= totalPokemonCount; i++)
	{
		state.pokemons[i].currentHp = baseData[i].maxHp;
		for (int m = 0; m < MOVE_COUNT; ++m)
			state.pokemons[i].remainingMoveCount[m] = baseData[i].maxMoveCount[m];
		state.pokemons[i].location = (i == 0) ? Location::HAND : Location::WILD;
	}

	// 全WILDポケモンをスコア降順でソートし、初回戦略決定
	vector<int> sortedRank;
	for (int i = 1; i <= totalPokemonCount; i++) sortedRank.push_back(i);
	sort(sortedRank.begin(), sortedRank.end(), [&](int a, int b) {
		return baseData[a].score > baseData[b].score;
		});

	// 初回は currentHp == maxHp なので statePtr に nullptr（maxHpベース）を渡して計算
	int defeatCount = CalcDefeatCount(sortedRank, baseData, baseData[0].totalDamage, nullptr);

	// ターゲットを更新
	state.defeatTargets.assign(sortedRank.begin(), sortedRank.begin() + defeatCount);
	state.catchTargets.assign(sortedRank.begin() + defeatCount, sortedRank.end());
	// 初期の手持ちはID=0のポケモン1匹のみ
	state.handIds = { 0 };
	// フェーズ1の状態を初期化
	state.bIsPhase1Complete = state.catchTargets.empty();


	////////////////////
	// メインループ   //
	////////////////////

	while (true)
	{
		// ステップ1：ストック済みポケモンを捕まえる
		// （捕獲・再決定が行われた場合は continue でループの先頭に戻る）
		if (TryCatchStocked(state, baseData, totalPokemonCount)) continue;

		// ステップ2：技が尽きた手持ちをボックスへ送る
		BoxDepletedHand(state);

		// 手持ち全員が技切れならループ終了
		if (!HasAnyMove(state)) break;

		// ステップ3：技を使う行動（フェーズ1またはフェーズ2）
		// 何も行動できなければループ終了
		bool bHasActed = ExecuteAttack(state, baseData);
		if (!bHasActed) break;
	}


	// 提出時、以下はコメントアウト
	/** ここから */
//	cin.rdbuf(oldIn);
//	cout.rdbuf(oldOut);
//}
/** ここまで */

	return 0;
}